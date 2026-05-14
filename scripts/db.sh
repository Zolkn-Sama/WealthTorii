#!/usr/bin/env bash
# Convenience wrapper around the WealthTorii Postgres dev container.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPOSE="docker compose -f ${ROOT}/infra/docker-compose.yml"
export DATABASE_URL="${DATABASE_URL:-postgresql://wealthtorii:wealthtorii@localhost:5544/wealthtorii}"

cmd="${1:-help}"
case "${cmd}" in
    up)
        ${COMPOSE} up -d
        echo "DATABASE_URL=${DATABASE_URL}"
        ;;
    down)
        ${COMPOSE} down "${@:2}"
        ;;
    psql)
        ${COMPOSE} exec -T db psql -U wealthtorii -d wealthtorii "${@:2}"
        ;;
    reset)
        ${COMPOSE} down -v
        ${COMPOSE} up -d
        ;;
    logs)
        ${COMPOSE} logs -f
        ;;
    url)
        echo "${DATABASE_URL}"
        ;;
    help|*)
        cat <<USAGE
WealthTorii db helper.
  $(basename "$0") up      -- start Postgres in the background, print URL
  $(basename "$0") down    -- stop (keeps volume)
  $(basename "$0") reset   -- stop + wipe volume + restart fresh
  $(basename "$0") psql    -- open a psql shell
  $(basename "$0") logs    -- tail container logs
  $(basename "$0") url     -- print the DATABASE_URL
USAGE
        ;;
esac
