# 错误码表

定义位置：`src/public/const.h`（`enum ErrorCode`）。HTTP 响应的 `error` 字段、gRPC 响应的 `error` 字段、TCP 登录响应帧的 `error` 字段**共用同一套编码**。

## 1. 编码表

| 值 | 枚举 | 含义 | 典型出现位置 |
|----|------|------|-------------|
| `0` | `SUCCESS` | 成功 | 全部接口 |
| `1001` | `ERROR_JSON` | JSON 解析失败或必填字段缺失 | GateServer HTTP 各接口 |
| `1002` | `RPC_FAILED` | 下游 gRPC 调用失败（含超时、连接池不可用） | `/get_Verifycode`、`/user_login`、TCP 登录响应 |
| `1003` | `VERIFY_CODE_EXPIRED` | 验证码不存在或已过期（Redis 无 `code_<email>`） | `/user_register`、`/reset_password` |
| `1004` | `VERIFY_CODE_ERROR` | 验证码错误 | `/user_register`、`/reset_password` |
| `1005` | `USER_EXIST` | 用户已存在（预留，当前由存储过程返回码映射） | —— |
| `1006` | `PASSWORD_ERROR` | 密码错误（或注册时两次密码不一致） | `/user_login`、`/user_register` |
| `1007` | `EMAIL_NOT_MATCH` | uid 与邮箱不匹配 | `/reset_password` |
| `1008` | `PASSWORD_UP_FAILED` | 密码更新失败 | `/reset_password` |
| `1009` | `PASSWORD_INVALID` | 密码无效（预留，当前未返回） | —— |
| `1010` | `REGISTER_USER_FAILED` | 用户注册失败 | `/user_register` |
| `1011` | `USER_UID_INVALID` | 非法/不存在的用户 id | StatusServer `Login`、TCP 登录响应 |
| `1012` | `USER_TOKEN_INVALID` | token 非法或已失效 | StatusServer `Login`、TCP 登录响应 |
| `1013` | `SERVICE_UNAVAILABLE` | **依赖服务不可用**（数据库/Redis 连接池超时或不可达） | `/user_login`、`/reset_password`（本次新增） |

## 2. 本次变更

- **新增** `1013 SERVICE_UNAVAILABLE`，用于把「后端故障」与「业务失败」区分开。
- **语义调整（可能影响既有客户端判断）**：
  - `/user_login`：数据库不可用时，历史版本返回 `1006 PASSWORD_ERROR`，现在返回 `1013`；
  - `/reset_password`：数据库不可用时，历史版本返回 `1007 EMAIL_NOT_MATCH`，现在返回 `1013`。
  - 客户端若此前把 `1006` / `1007` 同时用于"提示用户重试"，建议改为对 `1013` 做"稍后重试"提示、对 `1006` / `1007` 做"凭据错误"提示。

## 3. 判定原则

| 场景 | 返回 |
|------|------|
| 请求本身不合法（缺字段、JSON 语法错误） | `1001` |
| 业务规则不满足（验证码错、密码错、邮箱不匹配） | 对应的 `1003` / `1004` / `1006` / `1007` |
| 依赖服务不可用（连接池取不到连接、RPC 超时/失败） | `1013`（数据库/Redis）或 `1002`（gRPC） |
| 服务端内部失败（更新/注册未生效） | `1008` / `1010` |

> 排查建议：`1013` / `1002` 应同时检查 MySQL(3308)、Redis(6380)、StatusServer(50052)、VerifyServer(50051) 的可用性；两个连接池的获取连接超时均为 **2 秒**。
