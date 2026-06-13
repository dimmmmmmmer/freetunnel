#include "AppUiUtils.h"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QApplication>

QString shellEscape(QString s) {
    s.replace("'", "'\"'\"'");
    return "'" + s + "'";
}

QString appleScriptEscape(QString s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    return s;
}

bool runElevatedShell(const QString &command, QString *errorText) {
    const QString script =
            QString("do shell script \"%1\" with administrator privileges").arg(appleScriptEscape(command));

    QProcess p;
    p.start("osascript", {"-e", script});
    if (!p.waitForFinished(30000)) {
        if (errorText) {
            *errorText = "osascript timed out";
        }
        p.kill();
        return false;
    }

    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (errorText) {
            const QString stdErr = QString::fromLocal8Bit(p.readAllStandardError());
            const QString stdOut = QString::fromLocal8Bit(p.readAllStandardOutput());
            *errorText = QString("osascript failed (code=%1). %2 %3")
                                 .arg(p.exitCode())
                                 .arg(stdErr.trimmed())
                                 .arg(stdOut.trimmed());
        }
        return false;
    }

    return true;
}

QIcon makeAppIcon() {
    QPixmap pm;
    if (!pm.load(":/assets/logo.png")) {
        QString assetPath = QCoreApplication::applicationDirPath() + "/assets/logo.png";
        if (!QFile::exists(assetPath)) {
            const QString alt = QStandardPaths::locate(QStandardPaths::AppDataLocation, "assets/logo.png");
            if (!alt.isEmpty()) assetPath = alt;
        }
        pm.load(assetPath);
    }
    if (pm.isNull()) {
        pm = QPixmap(256, 256);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QLinearGradient bg(0, 0, 256, 256);
        bg.setColorAt(0.0, QColor(26, 117, 255));
        bg.setColorAt(1.0, QColor(0, 200, 170));
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(8, 8, 240, 240, 56, 56);
        p.setPen(QPen(QColor(255, 255, 255), 18, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(78, 128, 178, 128);
        p.drawLine(92, 92, 78, 128);
        p.drawLine(92, 164, 78, 128);
        p.drawLine(164, 92, 178, 128);
        p.drawLine(164, 164, 178, 128);
        p.end();
    }
    return QIcon(pm);
}
