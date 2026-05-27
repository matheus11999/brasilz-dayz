# BrasilZ Portal Webhook API

Este documento descreve o contrato para a API externa que vai receber eventos do servidor BrasilZ e montar ranking, kill feed, estatisticas de mortes, kills, zombies, bandidos, tempo vivo e tiros mais longos.

## Visao Geral

O mod envia eventos HTTP `POST` para a URL configurada em:

`Scripts/Game/BrasilZ/Portal/BZ_PortalConfig.c`

Configure:

```c
static const string ENDPOINT_URL = "https://sua-api.com/v1/arma/events";
static const string API_KEY = "troque-por-um-segredo-forte";
static const string SERVER_ID = "brasilz-main";
```

Se `ENDPOINT_URL` ou `API_KEY` estiver vazio, o envio fica desativado.

## Seguranca

Cada request envia a API key no header:

```http
X-BrasilZ-Api-Key: troque-por-um-segredo-forte
Content-Type: application/json
User-Agent: BrasilZ-ArmaReforger
```

A API receptora deve:

1. Rejeitar requests sem `X-BrasilZ-Api-Key`.
2. Comparar a key usando comparacao constante, se disponivel.
3. Aceitar somente HTTPS em producao.
4. Responder rapido com `2xx`; processamentos pesados devem ir para fila.
5. Registrar `server_id`, `event_type` e `timestamp_unix`.

Nao coloque a API key no client, Discord, UI ou arquivo enviado para jogadores. Ela deve existir somente no servidor.

## Envelope

Todos os eventos seguem o mesmo envelope:

```json
{
  "schema_version": "1.0",
  "server_id": "brasilz-main",
  "event_type": "player_killed",
  "timestamp_unix": 1779828406,
  "data": {}
}
```

## Eventos

### `player_connected`

Enviado quando um player conecta.

```json
{
  "player": {
    "player_id": 1,
    "name": "mateus11martins",
    "uid": "92f30ed0-9121-4887-8a01-fe8bbf80adf7",
    "position": {"x": 14260.4, "y": 5.3, "z": 13041.1},
    "prefab": "{...}Prefabs/Characters/Character_BrasilZ_Survivor.et"
  },
  "first_time": false,
  "online_count": 1,
  "balance": {"wallet": 1000, "loose": 50, "total": 1050}
}
```

Uso recomendado: atualizar ultimo login, criar jogador caso nao exista, contar players unicos.

### `player_disconnected`

```json
{
  "player": {},
  "cause_code": 0,
  "timeout": -1,
  "balance": {"wallet": 1000, "loose": 50, "total": 1050}
}
```

Uso recomendado: atualizar ultimo logout e saldo observado.

### `player_spawned`

```json
{
  "player": {},
  "prefab": "{...}Prefabs/Characters/Character_BrasilZ_Survivor_M70.et",
  "spawn_point": "Rify",
  "position": {"x": 13700, "y": 2.46, "z": 11200},
  "balance": {"wallet": 0, "loose": 0, "total": 0}
}
```

`spawn_point` aparece no respawn direto aleatorio. Em alguns spawns vanilla pode vir ausente.

Uso recomendado: iniciar uma nova vida do player e salvar local de spawn.

### `player_killed`

Evento principal para ranking e kill feed.

```json
{
  "victim": {
    "player_id": 1,
    "name": "mateus11martins",
    "uid": "92f30ed0-9121-4887-8a01-fe8bbf80adf7",
    "position": {"x": 7222.7, "y": 6.0, "z": 2616.7},
    "prefab": "{...}Character_BrasilZ_Survivor_M70.et"
  },
  "victim_balance": {"wallet": 0, "loose": 0, "total": 0},
  "victim_stats": {
    "hydration": 0.94,
    "energy": 0.95,
    "bleeding": true
  },
  "alive_seconds": 1842,
  "killer": {
    "type": "player",
    "name": "OutroPlayer",
    "prefab": "{...}Character_BrasilZ_Survivor.et",
    "player": {
      "player_id": 2,
      "name": "OutroPlayer",
      "uid": "uuid-do-killer"
    }
  },
  "killer_balance": {"wallet": 500, "loose": 0, "total": 500},
  "weapon": {
    "name": "Rifle_AUG_base",
    "prefab": "{053B975DDBCAF1B0}Prefabs/Weapons/Rifles/556/Rifle_AUG_base.et"
  },
  "distance_m": 123.4,
  "is_pvp": true,
  "is_suicide": false,
  "title": "PvP Kill"
}
```

Valores de `killer.type`:

- `player`: outro jogador matou.
- `zombie`: prefab contem `Zombie`, `Infected` ou `BaconZ`.
- `bandit`: prefab contem `PLASTICBANDIT` ou `Bandit`.
- `npc`: outro NPC.
- `environment`: queda, afogamento, fome, sede, sangramento sem killer, etc.
- `suicide`: killer e vitima sao o mesmo player.

Uso recomendado:

- `victim.uid`: incrementar deaths.
- `killer.player.uid`: incrementar player kills quando `killer.type == "player"`.
- `killer.type == "zombie"`: incrementar deaths_by_zombie.
- `killer.type == "bandit"`: incrementar deaths_by_bandit.
- `alive_seconds`: calcular maior tempo vivo e tempo medio.
- `weapon.name` + `distance_m`: kill feed, arma mais usada, tiros mais longos.

### `shop_purchase`, `shop_purchase_failed`, `shop_sale`

```json
{
  "player": {},
  "item": {
    "name": "SleepingBag",
    "prefab": "{...}Prefabs/Items/SleepingBag.et"
  },
  "quantity": 1,
  "success": true,
  "is_purchase": true,
  "price": 5000,
  "balance": {"wallet": 10000, "loose": 0, "total": 10000}
}
```

Uso recomendado: historico economico, item mais comprado/vendido, deteccao de abuso.

### `mission_started`

```json
{
  "mission": "INVASAO em Pavlovo MB",
  "sub_idx": 13,
  "active_count": 1
}
```

### `mission_ended`

```json
{
  "mission": "INVASAO em Pavlovo MB",
  "sub_idx": 13,
  "won": false,
  "cooldown_seconds": 3600
}
```

## Exemplo de Receiver Node/Express

```js
import express from "express";
import crypto from "node:crypto";

const app = express();
app.use(express.json({ limit: "128kb" }));

const API_KEY = process.env.BRASILZ_API_KEY;

function safeEqual(a, b) {
  const ab = Buffer.from(a || "");
  const bb = Buffer.from(b || "");
  return ab.length === bb.length && crypto.timingSafeEqual(ab, bb);
}

app.post("/v1/arma/events", async (req, res) => {
  const key = req.header("x-brasilz-api-key");
  if (!safeEqual(key, API_KEY)) return res.status(401).json({ error: "invalid api key" });

  const event = req.body;
  if (!event || !event.event_type || !event.server_id || !event.data) {
    return res.status(400).json({ error: "invalid payload" });
  }

  // Salve o evento bruto primeiro para auditoria/idempotencia.
  // Depois processe por event_type em uma fila/job.
  console.log(event.event_type, event.server_id, event.timestamp_unix);

  return res.status(204).end();
});

app.listen(3000);
```

## Observacoes de Implementacao

- O servidor envia no maximo um request por evento de jogo.
- O mod nao reenvia em caso de falha; se precisar garantia forte, implemente uma fila local no futuro.
- `distance_m` vem `-1` quando nao existe killer com entidade valida.
- `weapon.name` pode ser `(desconhecida)` ou `(maos/vazio)` para mortes ambientais, zombies sem arma ou edge cases.
- `alive_seconds` e contado por UID desde connect/spawn ate morte. Em respawn, uma nova vida e iniciada.
- Eventos de restart de servidor nao foram incluidos, conforme solicitado.
