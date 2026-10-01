#!/usr/bin/env bash
# Refuse a release unless the Tests and Security workflows passed on the very
# commit being released. Run by the release job in .github/workflows/build.yml.
#
# A v* tag publishes from the Build workflow, and `needs:` only reaches jobs of
# the same run: Tests and Security are runs of their own, so without this a tag
# published whatever they said, or before they had said anything. The API is
# asked instead, for each workflow, which of its runs tested this commit.
#
# A pull request's runs do not count. Their head_sha is the branch's last
# commit, but what they built and tested is that commit merged into the base
# branch as it then was, which is not necessarily what is being released.
#
# Passes once every workflow has a successful run on the commit. Waits while a
# run is still going and none has passed yet. Fails, naming the runs, when every
# run finished and none passed, or when there is no run at all.
#
# usage: check-release-ci.sh <commit-sha> <workflow-file>...
# env:   GH_TOKEN and GITHUB_REPOSITORY, as Actions provides them;
#        WAIT_MINUTES (default 120) and POLL_SECONDS (default 60).
set -euo pipefail

sha="${1:?usage: check-release-ci.sh <commit-sha> <workflow-file>...}"
shift
if [[ "$#" -eq 0 ]]; then
  echo "usage: check-release-ci.sh <commit-sha> <workflow-file>..." >&2
  exit 2
fi
repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}"
wait_minutes="${WAIT_MINUTES:-120}"
poll="${POLL_SECONDS:-60}"
deadline=$(( $(date +%s) + wait_minutes * 60 ))

# One line per run of workflow $1 that tested this commit:
# status, conclusion, event, URL. Three attempts: an API hiccup should not cost
# a release, and a real permission error fails the same way three times.
runs_of() {
  local json attempt
  for attempt in 1 2 3; do
    if json="$(gh api -X GET "repos/$repo/actions/workflows/$1/runs" \
                 -f head_sha="$sha" -f per_page=100)" \
       && jq -r '.workflow_runs[]
                 | select(.event != "pull_request" and .event != "pull_request_target")
                 | [.status, (.conclusion // "none"), .event, .html_url]
                 | @tsv' <<<"$json"; then
      return 0
    fi
    if [[ "$attempt" -lt 3 ]]; then
      sleep "$poll"
    fi
  done
  return 1
}

# 0: a run passed. 2: none has passed and one is still going. 1: otherwise.
verdict() {
  local wf="$1" runs
  if ! runs="$(runs_of "$wf")"; then
    echo "::error title=Cannot read $wf runs::could not list the $wf runs of $sha (does the job have actions: read?)"
    return 1
  fi
  if [[ -z "$runs" ]]; then
    echo "::error title=$wf has not run::$wf has no run on $sha outside pull requests. Run it on the tag (gh workflow run $wf --ref ${GITHUB_REF_NAME:-<tag>}), then re-run this job."
    return 1
  fi
  if awk -F'\t' '$1 == "completed" && $2 == "success" { found = 1 } END { exit !found }' <<<"$runs"; then
    echo "$wf passed on $sha"
    return 0
  fi
  if awk -F'\t' '$1 != "completed" { found = 1 } END { exit !found }' <<<"$runs"; then
    return 2
  fi
  echo "::error title=$wf did not pass::no $wf run on $sha succeeded, so this commit is not released:"
  awk -F'\t' '{ printf "  %s (%s): %s\n", $2, $3, $4 }' <<<"$runs"
  return 1
}

pending=("$@")
while :; do
  still=()
  for wf in "${pending[@]}"; do
    rc=0
    verdict "$wf" || rc=$?
    case "$rc" in
      0) ;;
      2) still+=("$wf") ;;
      *) exit 1 ;;
    esac
  done
  if [[ "${#still[@]}" -eq 0 ]]; then
    echo "Every required workflow passed on $sha."
    exit 0
  fi
  if [[ "$(date +%s)" -ge "$deadline" ]]; then
    echo "::error title=Still running::${still[*]} had not finished on $sha after ${wait_minutes} minutes. Re-run this job once they pass."
    exit 1
  fi
  echo "Waiting for ${still[*]} to finish on $sha…"
  sleep "$poll"
  pending=("${still[@]}")
done
