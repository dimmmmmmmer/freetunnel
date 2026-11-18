// cppcheck-suppress-file missingIncludeSystem
#include "qt_trusttunnel_client.h"
#include "core/AppRules.h"

#include <QMetaObject>
#include <QThread>
#include <exception>
#include <utility>

void QtTrustTunnelClient::reportEndpointRejection(const QString &error, quint64 requestId)
{
    emit endpointRequestFinished(requestId, false);
    emit vpnError(error);
    emit stateChanged(m_state.load());
}

void QtTrustTunnelClient::beginConnectConfigured(const QString &toml, const QStringList &domains,
        bool selective, const QStringList &apps, quint64 requestId)
{
    if (!m_stopRequested && (m_client || (m_connectThread && m_connectThread->isRunning()))) {
        EndpointSwitchRequest request{toml, domains, selective, freetunnel::sanitizedAppRules(apps), requestId,
                ++m_endpointRequest, m_publishedRouting.generation};
        if (m_endpointPreparationActive || !m_client) {
            m_pendingEndpoint = std::move(request);
            return;
        }
        prepareEndpointSwitch(std::move(request));
        return;
    }
    QString error;
    if (!parseConfig(toml, &error)) {
        reportEndpointRejection(error, requestId);
        return;
    }
    setSplitRouting(domains, selective, apps);
    beginConnect(toml);
    emit endpointRequestFinished(requestId, true);
}

void QtTrustTunnelClient::resumePendingEndpoint()
{
    if (!m_pendingEndpoint || m_endpointPreparationActive)
        return;
    if (!m_client && m_connectThread && m_connectThread->isRunning()) {
        QTimer::singleShot(20, this, &QtTrustTunnelClient::resumePendingEndpoint);
        return;
    }
    auto request = std::exchange(m_pendingEndpoint, std::nullopt);
    if (m_client && !m_stopRequested) {
        prepareEndpointSwitch(std::move(*request));
    } else {
        // The initial attempt failed before owning a usable session. Its worker
        // is finished, so the latest explicit pick may start a fresh one now.
        beginConnectConfigured(request->toml, request->domains, request->selective,
                               request->apps, request->id);
    }
}

void QtTrustTunnelClient::switchEndpoint(const QString &toml)
{
    if (!m_client || m_stopRequested) {
        reportEndpointRejection(tr("No running VPN session"), 0);
        return;
    }
    QStringList domains;
    for (const auto &domain : m_extraExclusions)
        domains.append(QString::fromStdString(domain));
    beginConnectConfigured(toml, domains, m_selectiveMode, m_appRuleList, 0);
}

void QtTrustTunnelClient::prepareEndpointSwitch(EndpointSwitchRequest request)
{
    QString error;
    auto config = parseConfig(request.toml, &error);
    if (!config) {
        reportEndpointRejection(error, request.id);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        const auto ports = config->killswitch_allow_ports;
        clearKeysRootMustNotTakeFromAConfig(*config);
        config->endpoint_sockets_protected = true;
        config->killswitch_enabled = m_killSwitch;
        config->killswitch_allow_ports = m_killSwitchPortsFromConfig ? ports : std::string();
        auto &tun = std::get<ag::TrustTunnelConfig::TunListener>(config->listener);
        tun.excluded_routes.insert(tun.excluded_routes.end(), m_extraExcludedRoutes.begin(), m_extraExcludedRoutes.end());
    }
    m_endpointPreparationActive = true;
    m_preserveEndpointSession = true;
    const quint64 session = m_sessionGen.load();
    const auto guard = m_guard;
    // No live core pointer crosses into this worker. Disconnect/quit can win
    // immediately even when resolving the replacement takes its full deadline.
    QThread *worker = QThread::create([this, guard, request, session, config = std::move(*config)]() mutable {
        QString error;
        std::shared_ptr<ag::TrustTunnelClient::PreparedEndpoint> prepared;
        try {
            auto result = ag::TrustTunnelClient::prepare_endpoint(std::move(config));
            if (result.has_error())
                error = QString::fromStdString(result.error()->str());
            else
                prepared = std::move(result.value());
        } catch (const std::exception &exception) {
            error = QString::fromUtf8(exception.what());
        }
        std::lock_guard<std::mutex> lock(guard->mutex);
        if (!guard->alive)
            return;
        QMetaObject::invokeMethod(this, [this, request, session, prepared, error]() {
            finishEndpointPreparation(request, session, prepared, error);
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

std::pair<QtTrustTunnelClient::Routing, std::vector<std::string>>
QtTrustTunnelClient::routingForEndpoint(const EndpointSwitchRequest &request, const ag::TrustTunnelConfig &config) const
{
    // An explicit rule edit made during preparation wins over the staged pick.
    const bool edited = request.baseGeneration != m_publishedRouting.generation;
    const bool selective = edited ? m_selectiveMode : request.selective;
    std::vector<std::string> domains;
    if (edited) {
        domains = m_extraExclusions;
    } else {
        for (const auto &domain : request.domains)
            domains.push_back(domain.toStdString());
    }
    Routing wanted;
    wanted.mode = selective ? ag::VPN_MODE_SELECTIVE : ag::VPN_MODE_GENERAL;
    wanted.exclusions = wanted.mode == config.mode ? config.exclusions : std::string();
    for (const auto &domain : domains) {
        if (!wanted.exclusions.empty() && wanted.exclusions.back() != ' ')
            wanted.exclusions += ' ';
        wanted.exclusions += domain;
    }
    wanted.appRules = edited ? m_appRuleList : request.apps;
    wanted.generation = m_publishedRouting.generation;
    if (wanted != m_publishedRouting)
        ++wanted.generation;
    return {std::move(wanted), std::move(domains)};
}

void QtTrustTunnelClient::finishEndpointPreparation(const EndpointSwitchRequest &request, quint64 session,
        const std::shared_ptr<ag::TrustTunnelClient::PreparedEndpoint> &prepared, const QString &error)
{
    m_endpointPreparationActive = false;
    if (m_pendingEndpoint) {
        // The old preparation can finish after disconnect and a fresh startup.
        // Keep that new session's pick queued until its client can be adopted.
        resumePendingEndpoint();
        return;
    }
    if (request.serial != m_endpointRequest || session != m_sessionGen || m_stopRequested || !m_client)
        return;
    if (!error.isEmpty()) {
        reportEndpointRejection(error, request.id);
        return;
    }
    QString ignored;
    auto config = parseConfig(request.toml, &ignored);
    Q_ASSERT(config);
    auto [wanted, domains] = routingForEndpoint(request, *config);
    const ag::TrustTunnelClient::EndpointRouting routing{wanted.mode, wanted.exclusions, wanted.generation};
    const auto failure = m_client->replace_endpoint(prepared, [this, &wanted]() noexcept {
        // This runs at the core's commit boundary, between the old handshake
        // and the new one. App decisions and domain rules share one generation.
        std::lock_guard<std::mutex> lock(m_appRules->mutex);
        m_appRules->rules = wanted.appRules;
        m_appRules->selective = wanted.mode == ag::VPN_MODE_SELECTIVE;
        m_appRules->generation = wanted.generation;
        ++m_endpointEpoch;
    }, &routing);
    if (failure) {
        reportEndpointRejection(QString::fromStdString(failure->str()), request.id);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_lastConfigToml = request.toml;
        m_originalExclusions = config->exclusions;
        m_originalMode = config->mode;
        m_originalExcludedRoutes = std::get<ag::TrustTunnelConfig::TunListener>(config->listener).excluded_routes;
        m_configAllowPorts = config->killswitch_allow_ports;
        m_extraExclusions = std::move(domains);
        m_selectiveMode = wanted.mode == ag::VPN_MODE_SELECTIVE;
        m_config.reset();
    }
    m_appRuleList = wanted.appRules;
    m_publishedRouting = wanted;
    m_sessionRouting = wanted;
    m_firstConnectFailuresSaid.clear();
    m_reconnectTimer.stop();
    emit endpointRequestFinished(request.id, true);
    setState(State::Reconnecting);
}
