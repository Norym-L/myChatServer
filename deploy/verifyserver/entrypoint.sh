#!/bin/sh
# verifyserver 容器入口：把 config.json.tmpl 按环境变量渲染成 /app/config.json
# （src/config.js 用 readFileSync('./config.json')，相对 CWD，所以必须在 /app 下执行）
set -eu

export SMTP_USER="${SMTP_USER:-}"
export SMTP_PASS="${SMTP_PASS:-}"
export MYSQL_HOST="${MYSQL_HOST:-mysql}"
export MYSQL_PORT="${MYSQL_PORT:-3306}"
export MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
export REDIS_HOST="${REDIS_HOST:-redis}"
export REDIS_PORT="${REDIS_PORT:-6379}"
export REDIS_PASSWORD="${REDIS_PASSWORD:-123456}"

if [ -z "${SMTP_USER}" ] || [ -z "${SMTP_PASS}" ]; then
    echo "[verify] 警告：SMTP_USER / SMTP_PASS 没配置，注册时发不出验证码" >&2
fi

cd /app
envsubst < /app/config.json.tmpl > /app/config.json
echo "[verify] 渲染后的 config.json："
sed 's/"pass": *"[^"]*"/"pass": "***"/' /app/config.json | sed 's/^/    | /'

exec node src/server.js
