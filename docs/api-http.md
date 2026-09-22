# HTTP 接口（GateServer）

Base URL：`http://<gate-host>:8080`

## 1. 公共约定

| 项 | 约定 |
|----|------|
| 请求体 | JSON（`application/json`），由 `Json::Reader` 解析；解析失败或必填字段缺失 → `error = 1001` |
| 成功响应 | HTTP `200`，响应体为 JSON，`Content-Type: text/json`，`Server: GateServer` |
| 业务失败 | **同样是 HTTP 200**，通过响应体的 `error` 字段区分（`0` 为成功） |
| 未知路径 | HTTP `404` + `Content-Type: text/plain`，正文 `url not found` |
| 不支持的方法 | HTTP `405` + `Content-Type: text/plain`，正文 `method not allowed`（`HEAD` 返回 405 且不带正文） |
| 连接 | 服务端固定 `Connection: close`；请求读取超时 **60 秒** |
| 请求体大小 | 读取缓冲区 `8192` 字节，超出会被中间件拒绝 |
| 错误码 | 见 [`error-codes.md`](error-codes.md) |

> ⚠️ `user` 字段在不同接口中语义不同：在 `/user_register` 中表示**昵称（字符串）**，在 `/user_login`、`/reset_password` 中表示 **uid（数字）**。

## 2. 接口一览

| 方法 | 路径 | 用途 | 变更状态 |
|------|------|------|---------|
| GET | `/get_test` | 调试：回显查询参数 | 行为变更（参数解码修正） |
| POST | `/get_Verifycode` | 获取邮箱验证码 | 修改（新增必填校验） |
| POST | `/user_register` | 用户注册 | 修改（**响应移除 `password`**） |
| POST | `/reset_password` | 重置密码 | 修改（**字段 `varifycode` → `verify_code`**） |
| POST | `/user_login` | 用户登录 | 修改（新增 `1013`） |

---

## 3. GET /get_test

**用途**：调试接口，回显本次请求的全部查询参数。

**请求参数**：任意 query string，例如 `?a=%41&b=hello+world`

**响应**：HTTP 200，纯文本（未设置 `Content-Type`），形如：

```
receive get_test reqparam 1 key is b value is hello world
param 2 key is a value is A
```

> 说明：参数按 URL 解码后输出；`+` 解为空格，`%XX` 解为对应字节；截断或非法的 `%` 序列（如 `%ZZ`、`%4`）按原样保留。

---

## 4. POST /get_Verifycode

**用途**：请求向指定邮箱下发验证码（GateServer 转调 VerifyServer，验证码以 `code_<email>` 存入 Redis）。

**请求**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `email` | string | 是 | 接收验证码的邮箱 |

```json
{ "email": "user@example.com" }
```

**响应**

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | `0` 成功；`1001` 参数缺失；`1002` RPC 调用失败 |
| `email` | string | 回显请求邮箱 |
| `verify_code` | string | **仅 Debug 构建（`GATE_DEBUG`）返回**，用于本地联调；Release 构建不返回 |

```json
{ "error": 0, "email": "user@example.com" }
```

---

## 5. POST /user_register

**用途**：注册新用户；需先通过 `/get_Verifycode` 取得验证码。

**请求**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `email` | string | 是 | 邮箱 |
| `user` | string | 是 | **昵称**（非 uid） |
| `password` | string | 是 | 明文密码，服务端以 Argon2id 加盐哈希后入库 |
| `confirm` | string | 是 | 确认密码，须与 `password` 一致 |
| `verify_code` | string | 是 | 邮箱验证码 |

```json
{ "email": "user@example.com", "user": "norym", "password": "P@ssw0rd", "confirm": "P@ssw0rd", "verify_code": "123456" }
```

**响应（成功）**

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | `0` |
| `uid` | int | 新用户 id |
| `email` | string | 回显邮箱 |
| `user` | string | 回显昵称 |

> ⚠️ 响应**不再回显 `password`**（历史版本会返回明文密码）。

**错误码**

| error | 含义 |
|-------|------|
| `1001` | JSON 解析失败，或缺少 `email` / `user` / `password` / `confirm` / `verify_code` |
| `1006` | `password` 与 `confirm` 不一致 |
| `1003` | 验证码不存在或已过期（Redis 中无 `code_<email>`） |
| `1004` | 验证码错误 |
| `1010` | 注册失败（存储过程 `reg_user` 返回 -1，通常为邮箱已存在） |

---

## 6. POST /reset_password

**用途**：通过邮箱验证码重置指定 uid 的密码。

**请求**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `user` | int | 是 | **uid**（数字） |
| `email` | string | 是 | 邮箱，须与该 uid 绑定 |
| `password` | string | 是 | 新密码 |
| `verify_code` | string | 是 | 邮箱验证码 |

```json
{ "user": 10001, "email": "user@example.com", "password": "NewP@ss", "verify_code": "123456" }
```

**响应（成功）**

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | `0` |
| `email` | string | 回显邮箱 |
| `verify_code` | string | 回显验证码 |

> ⚠️ 本次变更：旧版本响应字段名为 `varifycode` 且恒为空（代码读取了不存在的请求字段），现已更正为 `verify_code`；同时**不再回显 `passwd`（明文新密码）**。

**错误码**

| error | 含义 |
|-------|------|
| `1001` | JSON 解析失败，或缺少 `user` / `email` / `password` / `verify_code` |
| `1003` | 验证码不存在或已过期 |
| `1004` | 验证码错误 |
| `1013` | 依赖服务不可用（MySQL 连接池超时/异常） |
| `1007` | uid 与 email 不匹配 |
| `1008` | 密码更新失败 |

---

## 7. POST /user_login

**用途**：校验账号密码，并分配一台 ChatServer、签发登录 token。

**请求**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `user` | int | 是 | **uid**（数字） |
| `password` | string | 是 | 明文密码 |
| `email` | string | 否 | 仅用于响应回显，**不参与校验** |

```json
{ "user": 10001, "password": "P@ssw0rd" }
```

**响应（成功）**

| 字段 | 类型 | 说明 |
|------|------|------|
| `error` | int | `0` |
| `email` | string | 回显请求中的 `email`（未传则为空串） |
| `uid` | int | 用户 id |
| `token` | string | 一次性登录票据（UUID），用于连接 ChatServer |
| `host` | string | 分配到的 ChatServer 地址 |
| `port` | string | 分配到的 ChatServer 端口 |

```json
{ "error": 0, "email": "", "uid": 10001, "token": "1f0c...e9", "host": "127.0.0.1", "port": "8090" }
```

**错误码**

| error | 含义 |
|-------|------|
| `1001` | JSON 解析失败，或缺少 `user` / `password` |
| `1013` | 依赖服务不可用（MySQL 连接池超时/异常） |
| `1006` | 密码错误或用户不存在 |
| `1002` | 调用 StatusServer 获取聊天服失败（含超时） |

> 取得 `token` / `host` / `port` 后，客户端应连接该 ChatServer 并发送 `message_id = 0` 的登录帧，详见 [`api-tcp.md`](api-tcp.md)。

---

## 8. 调用示例

```bash
# 获取验证码
curl -X POST http://127.0.0.1:8080/get_Verifycode \
     -H "Content-Type: application/json" -d '{"email":"user@example.com"}'

# 注册
curl -X POST http://127.0.0.1:8080/user_register \
     -d '{"email":"user@example.com","user":"norym","password":"P@ssw0rd","confirm":"P@ssw0rd","verify_code":"123456"}'

# 登录
curl -X POST http://127.0.0.1:8080/user_login \
     -d '{"user":10001,"password":"P@ssw0rd"}'
```
