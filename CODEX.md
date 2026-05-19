# BrasilZ Codex Notes

## Projeto ativo

- Trabalhar no projeto `BrasilZ`, em:
  `C:\Users\Alienware\Documents\My Games\ArmaReforgerWorkbench\addons\BrasilZ`
- Nao aplicar mudancas no projeto `mulas2` a menos que o usuario peca explicitamente.
- O `cwd` da sessao pode estar em `mulas2`, mas isso nao significa que o projeto alvo seja `mulas2`.

## Mundo correto

- O mundo do BrasilZ fica em:
  `Worlds/chernarus.ent`
- Sempre manter `Worlds/chernarus.ent` como `SubScene`, porque o mundo original/parent nao e editavel diretamente.
- Mudancas do BrasilZ devem ser feitas em layers proprias dentro de `Worlds/chernarus_Layers/`, nunca editando o mundo original do parent.
- O `addon.gproj` do BrasilZ deve apontar para:
  `"{89446B179FB7D077}Worlds/chernarus.ent"`
- A mission principal do BrasilZ aponta para:
  `"{89446B179FB7D077}Worlds/chernarus.ent"`

## Parent do mapa

- Para modo DayZ/survival proprio do BrasilZ, usar o parent base:
  `"{F513898A573B9C3F}Worlds/ChernarusMinusBeta.ent"`
- Nao usar `"{1DF5E9C75A8714E7}Worlds/GM_Cherno/GM_Cherno.ent"` para gameplay final do BrasilZ, pois ele traz `GameMode_Editor_Full` e `MapEntity_Default1`.

## Layers

- As entidades do BrasilZ devem ficar em:
  `Worlds/chernarus_Layers/`
- O Workbench so mostrou as entidades depois que elas ficaram em um layer carregado do BrasilZ.
- Nao assumir que um novo layer vazio/novo sera exibido sem reload/registro.
- O usuario moveu o `MapEntity` para a layer `map`; respeitar isso antes de editar novamente.

## MapEntity

- Estado atual: o parent `ChernarusMinusBeta` ja cria `MapEntity_Default1`. Nao criar `BrasilZ_MapEntity_Chernarus2` em layer propria enquanto esse parent estiver ativo.
- Se outro `SCR_MapEntity` for criado, o log mostra:
  `Multiple map entities present!`
- O antigo `Worlds/chernarus_Layers/map.layer` foi removido para evitar esse conflito.

## Marker config

- Nao usar este valor no GameMode:
  `m_sMarkerCfgPath "{720E8E61D7692173}Configs/Map/MapMarkerConfig.conf"`
- Esse GUID gerou:
  `Wrong GUID/name for resource ... MapMarkerConfig.conf`
- O GUID correto atualmente resolvido via dependencia `DarcCore` (`631EE12D448D7FCC`) para `Configs/Map/MapMarkerConfig.conf` e:
  `"{720E8E61D7692172}Configs/Map/MapMarkerConfig.conf"`
- O `SCR_MapMarkerManagerComponent` deve existir no `GameMode_BrasilZ.et`, senao o mapa abre com `m_MarkerCfg` nulo.
- O `SCR_GroupsManagerComponent` tambem deve existir no `GameMode_BrasilZ.et`, senao `SCR_MapMarkerEntrySquadLeader` pode falhar com `m_OnPlayableGroupCreated` nulo.
- `Player Map Markers` (`5E92F5A4A1B75A75`) e `SH-PMM-SquadOnly` (`6738AD4392C94FF5`) nao devem ser dependencias do BrasilZ; se forem testados, carregar apenas no `config.json` do servidor.
- O `BZ_GroupsManagerComponent.OnPlayerRegistered()` apenas registra/loga o jogador sem colocar automaticamente em grupo. Nao chamar persistencia custom de player aqui.
- Jogadores devem nascer como CIV pela configuracao nativa: `SCR_Faction` CIV herdando de `Configs/Factions/CIV.conf`, `SCR_LoadoutManager.m_sAffiliatedFaction "CIV"`, `BZ_MenuSpawnLogic.m_sForcedFaction = "CIV"` e prefab `Character_BrasilZ_Survivor.et` herdando de personagem CIV. Nao usar scripts pos-spawn para trocar faccao com `SetAffiliatedFactionByKey`.
- O `SCR_GroupsManagerComponent` continua habilitado no GameMode, sem `m_bAllowGroupMenu 0`, para o jogador conseguir abrir o menu de grupos depois de spawnar e entrar manualmente em um grupo.
- A UI `BZ_DeployMenu_NoGroups.c` deve manter o fluxo vanilla de escolher spawn point, mas esconder botao/lista/painel de grupos apenas no deploy/role menu. Ela chama o hide imediatamente e tambem com atraso curto, porque alguns widgets vanilla sao montados depois de `OnMenuOpened`.

## GameMode

- O GameMode do BrasilZ fica em:
  `Prefabs/MP/Modes/BrasilZ/GameMode_BrasilZ.et`
- O layer deve criar:
  `BZ_GameMode BrasilZ_GameMode2 : "{04D21652D08B9876}Prefabs/MP/Modes/BrasilZ/GameMode_BrasilZ.et"`
- A layer principal `Worlds/chernarus_Layers/chernarus.layer` tambem deve criar:
  `SCR_FactionManager BrasilZ_FactionManager : "{4A188E44289B9A50}Prefabs/MP/Managers/Factions/FactionManager_Editor.et"`
- Esse FactionManager evita erros de `No faction manager found` em SDRC/AI sem reativar o setup manual do Bacon.
- O `BrasilZ_FactionManager` deve registrar a faccao tecnica dos infectados:
  `SCR_Faction "{622120A5448725E3}" : "{297E7D166247105C}622120A5448725E3/Configs/BaconZ_Faction.conf"`
- Essa faccao corresponde ao `BACON_622120A5448725E3_FACTION` usado nos prefabs que o Ambient Zombies spawna.
- O `BrasilZ_FactionManager` tambem deve registrar a CIV herdando explicitamente de:
  `SCR_Faction "{607AA5C7A94496DA}" : "{3FA20B01D950D31F}Configs/Factions/CIV.conf"`
- Excecao importante: com parent `GM_Cherno`, nao criar `BZ_GameMode BrasilZ_GameMode2`, porque o parent ja traz `GameMode_Editor_Full` e o engine gera `Multiple game mode entities present!`.
- Para o BrasilZ estilo DayZ/SurvivorZ, manter `BZ_GameMode BrasilZ_GameMode2` e usar parent `ChernarusMinusBeta`, nao `GM_Cherno`.
- Game Master oficial deve ser cenario separado, nao misturado no cenario survival. O cenario GM do BrasilZ fica em:
  `Missions/BrasilZChernarusGM.conf`
- Esse cenario GM aponta para:
  `"{1DF5E9C75A8714E7}Worlds/GM_Cherno/GM_Cherno.ent"`
  porque o parent ja traz `GameMode_Editor_Full`/estrutura de GM. Nao adicionar esse parent no `Worlds/chernarus.ent` survival.
- Para servidor dedicado abrir o GM, usar no `config.json`:
  `scenarioId: "{BFB2EBE28D360A9E}Missions/BrasilZChernarusGM.conf"`.
- Para acessar GM no dedicado, o jogador precisa ser host/admin: configurar `passwordAdmin` e/ou `admins` no `game` do `config.json`.
- Manter `SCR_SupportStationManagerComponent` no GameMode para evitar:
  `Support station exists with range but no SCR_SupportStationManagerComponent available on GameMode`

## Starter loadout

- O loadout inicial civil fica em:
  `Scripts/Game/BrasilZ/Spawning/BZ_StarterLoadout.c`
- O prefab base do survivor tambem deve manter o mesmo inventario inicial em:
  `Prefabs/Characters/Character_BrasilZ_Survivor.et`
- Regra importante corrigida pelo usuario: pode haver varios personagens/skins no menu de spawn para variar aparencia, mas todos precisam usar o mesmo tipo de loadout inicial civil.
- A variacao entre personagens deve ser apenas visual/civil: camisa, calca, botas, chapeu/boné etc. Nao usar roupa militar, colete militar, mochila militar, arma ou outro loadout diferente em um dos personagens.
- Todos os prefabs default listados em `BZ_RespawnSystemComponent.m_aDefaultCharacterPrefabs` devem receber o mesmo kit inicial via `BZ_StarterLoadout.Apply`, independentemente da aparencia escolhida.
- Nao confiar em inventario embutido diferente em cada prefab de personagem. Se forem criados novos survivors visuais (`Character_BrasilZ_Survivor_M70`, `M88`, `BDU`, `Worker` etc.), eles devem ser civis visuais e deixar o kit final padronizado pelo `BZ_StarterLoadout`.
- Kit inicial desejado: mapa FIA, lanterna, faca/bayonet basica, maca, garrafa de agua, wallet fisica e WW2 bike deployable.
- Nao usar cantil/canteen no loadout inicial. O item de agua deve ser garrafa:
  `"{2DA40953CC5C6D88}Prefabs/FoodDrink/WaterBottler.et"`
- Nao adicionar mochila, bussola ou morfina no loadout inicial.
- Manter `SCR_CharacterInventoryStorageComponent` e `CharacterWeaponSlotComponent` no prefab do survivor; sem eles alguns itens podem nao entrar no inventario no dedicado.
- O prefab `Character_BrasilZ_Survivor.et` deve herdar de um civil concreto, nao de `Character_CIV_Randomized.et`. O randomizado pode instanciar `Character_CIV_CottonShirt_1.et`/outros civis e ignorar parte do inventario inicial do BrasilZ.
- Nao existe mais `BZ_PlayerPersistenceComponent`/`BZ_PlayerNativePersistenceComponent` ativo. O `BZ_SpawnPointSpawnHandlerComponent.PostProcessSpawnedPlayer()` aplica o `BZ_StarterLoadout` nos spawns feitos pelo menu BrasilZ; saves/restores reais devem ser tratados pela persistencia nativa da BI antes do fluxo manual de spawn.

## Persistencia

- Estado atual escolhido: persistencia estilo SurvivorZ pura, usando o `SCR_PersistenceSystem` nativo da BI e o override de config do `FMSurvivalModPack`.
- `Missions/BrasilZChernarus.conf` e `Missions/BrasilZChernarusGM.conf` devem apontar para:
  `SystemsConfig "{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf"`
- Manter `FMSurvivalModPack` (`687D35E643472E87`) como dependencia; ele traz o override por path de `Configs/Systems/Persistence/GameMode/GameMaster.conf` com whitelist grande para itens, roupas e storages.
- Foram removidos/desativados do BrasilZ:
  - `EPF_PersistenceManagerComponent`
  - `SCR_SaveLoadComponent` custom no GameMode/layer
  - `BZ_PlayerPersistenceComponent`
  - `BZ_PlayerNativePersistenceComponent`
  - configs custom `Configs/Systems/BrasilZChimeraSystemsConfig.conf` e `Configs/Systems/Persistence/BrasilZPersistence.conf`
  - dependencias diretas de `Enfusion Database Framework` (`5D6EA74A94173EDF`) e `Enfusion Persistence Framework` (`5D6EBC81EB1842EF`)
- Nao reativar JSON de player em `$profile:BrasilZ/Players` sem pedido explicito. Esse fluxo causou restore duplicado, itens de roupa/faca tentando entrar como inventario e logs `Restored item did not fit inventory`.
- O servidor precisa ter `Save mission progression` habilitado no painel/config para a persistencia nativa da BI salvar/recarregar player e mundo entre restarts.
- Itens que nao estiverem na whitelist do `FMSurvivalModPack`/PersistenceConfigGroup podem nao persistir. Para item custom novo persistir, preferir adicionar na whitelist nativa, nao recriar JSON custom de player.
- Building System 2 deve persistir blocos basicos pelos prefabs com componente de persistencia + `SCR_PersistenceSystem`. Componentes `BLD_BedManagerComponent` e `BLD_DoorRaidManagerComponent` continuam opcionais; SurvivorZ tambem nao usa esses para o basico.
- `BZ_ClaimedVehicleRegistryComponent`/CarKey2 claimed vehicles esta desativado por enquanto a pedido do usuario. Nao recolocar esse componente no GameMode/layer sem pedir.
- Refactor de reconnect/disconnect estilo ReforgedZ:
  - `BZ_MenuSpawnLogic` agora seta controller `SAVE` e character `SAVE` (antes era `DELETE`). Personagem fica reservado para reconnect.
  - `BZ_GameMode.OnPlayerDisconnected` salva controller+character via `SCR_PersistenceSystem.Save` e dispara `OverwriteLatestSave(BLOCKING)` para flush imediato.
  - Se character no disconnect estiver `DEAD`/`INCAPACITATED`: `DecoupleDeadBody`/`DecoupleUnconsciousBody` -> `StopTracking`+`StartTracking` para o corpo virar entidade independente lootavel, sem ser deletado junto com o jogador. Skip do `super.OnPlayerDisconnected` para o body nao ser removido.
  - `BZ_GameMode.TrackCorpseForCleanup` + `TickCorpseCleanup` (intervalo 60s) deletam corpos apos `m_fCorpseLifetimeSec` (default 1200s = 20min) para nao acumular bonecos no mundo.
  - `BZ_GameMode.OnPlayerKilled` flagga UID em `BZ_PlayerDeathRegistry`, decopla corpo, salva controller e flusha disco — bloqueia exploit de morrer e ALT+F4 antes do save.
  - `modded SCR_PlayerController`/`modded SCR_BaseGameMode` nao sao usados; toda logica de disconnect concentrada em `BZ_GameMode`.
  - Anti-ALT+F4 morrendo: `BZ_Utils.IsCharacterDying(entity)` checa health<=0 ou lifeState DEAD/INCAPACITATED em `OnPlayerDisconnected`; se true, registra death flag persistente.
  - `modded SCR_ReconnectComponent` (`BZ_ReconnectComponent.c`) sobrescreve `GetReconnectState` para retornar `ENTITY_DISCARDED` se: UID em `BZ_PlayerDeathRegistry`, character INCAPACITATED, ou health<=0. `OnPlayerAuditTimeouted` chama `SaveAndRemoveCharacter` com retry de persistencia ACTIVE (max 10x500ms) + BLOCKING flush antes de deletar.
  - `BZ_PlayerDeathRegistry` mantem death flag em `$profile:BrasilZ/Deaths/<uid>.flag` (presenca do arquivo = morto). Survive restart. Limpo no `BZ_SpawnPointSpawnHandlerComponent.PostProcessSpawnedPlayer` quando o jogador sobe novo personagem pelo menu BrasilZ.
  - `BZ_Utils.GetPlayerUID(playerId)` usa `BackendApi.GetPlayerIdentityId` (mesma fonte que o ReforgedZ).
  - `BZ_MenuSpawnLogic.RequestPlayerData_S` espera ate 30s (`MAX_PERSISTENCE_ACTIVE_WAIT_MS`) o `SCR_PersistenceSystem` ficar `ACTIVE` antes de cair pro menu; reconectantes (entity na `m_ReconnectPlayerList`) pulam o wait pra nao estourar audit timeout.
  - `BZ_MenuSpawnLogic.OnPlayerDataLoaded_S` chama `super` primeiro pra base game rodar `ResolveReconnection` antes de qualquer menu.
  - `BZ_MenuSpawnLogic.OnPlayerCharacterLoaded_S` valida entity carregada do save antes do possess: death flag persistido (`BZ_PlayerDeathRegistry`), lifeState DEAD/INCAPACITATED, health<=0, posicao near-origin (0,0,0 bug). Rejeita -> deleta entity + `DoInitialSpawn_S`. Garante anti-ALT+F4 ate pos-restart do servidor.
- Autosave periodico (`BZ_GameMode`):
  - `m_fAutoSaveInterval` attribute (default 60s, 0 desabilita).
  - `TryStartAutoSave` espera `IsSavingPossible()` (retry 3s) antes de armar.
  - `PerformAutoSave` chama `OverwriteLatestSave(BLOCKING)` (nao cria save novo, sobrescreve o ativo pra preservar dados de jogador offline).
  - `ForceSaveNow` disponivel pra restart hooks (flag `SHUTDOWN`).
  - `StopAutoSave` cancela tick.
- Permanece valido: nao reativar JSON de inventario/posicao em `$profile:BrasilZ/Players`. A pasta nova `$profile:BrasilZ/Deaths` so guarda flag minima, nao snapshot de inventario.

## Loading screen

- O BrasilZ tenta substituir a tela vanilla por override do layout:
  `UI/Layouts/Menus/LoadingScreen/ScenarioLoadingScreen.layout`
- Manter tambem a copia lower-case:
  `UI/layouts/Menus/LoadingScreen/ScenarioLoadingScreen.layout`
  porque alguns mods/pacotes registram esse caminho com `layouts` minusculo, e servidor Linux pode ser sensivel a maiusculas/minusculas.
- Outros mods tambem substituem esse mesmo layout, incluindo `PLGCore`, `WCS_Interface` e ReforgedZ. Para o fullscreen do BrasilZ vencer, o BrasilZ precisa carregar depois deles no `config.json`, idealmente perto do fim da lista de mods.
- A imagem usada pelo override e:
  `"{CF459B79E65A7B58}UI/Images/carregando.edds"`

## Regras praticas

- Antes de editar, sempre verificar se o arquivo pertence ao `BrasilZ`.
- Ao investigar logs, separar erros de dependencias externas dos erros do BrasilZ.
- Nao remover MapEntity ou layers que o usuario acabou de mover pelo Workbench sem confirmar.
- Como o mapa ja foi validado funcionando, evitar qualquer alteracao em `Worlds/chernarus.ent`, `map.layer`, `chernarus.layer` e configs de mapa/markers, exceto se o usuario pedir exatamente isso.
- Se uma entidade aparece no log como criada, a layer esta carregando.
- Se uma entidade existe no arquivo mas nao aparece no editor, verificar se o mundo aberto e exatamente:
  `BrasilZ/Worlds/chernarus.ent`

## Shops, trader e safezones

- O usuario nao quer adicionar `SurvivorZ` como dependencia do BrasilZ.
- Nunca adicionar `SurvivorZ` (`693D19523E7D8E79`) nem `DZR Restored RUSS`/`DZRestored` (`68DB5125D9B16A0B`) como dependencia do BrasilZ.
- As layers de shop/trader/safezone devem continuar sem dependencia do SurvivorZ, mas podem usar dependencias independentes necessarias:
  - `ShopSystem` (`5D2D1436D1FA5A13`)
  - `ZeliksZones` (`5C6156F84AA262A2`)
  - `FMShop` (`66FD0AE74CD5162D`)
  - `British Forces` (`5AE50EC5B8D6F4AE`) para o mesmo helipad `E_Helipad_Lights_US_01.et` usado na referencia, sem depender de Wasteland
  - `Placeables for GM byHeine` (`61110CC4F1FF9C8A`) ou `Structures For GM byHeine` (`628EDA2ABC937159`) para a garagem `Garage_E_02_base.et`
- Rechecado no SurvivorZ atualizado `2.0.17`: os unicos GUIDs que ele tem a mais que o BrasilZ, alem do proprio SurvivorZ, sao:
  - `695251829BB1DE3D` = `Ambient Zombies FURTHER SPAWNS`
  - `5ED0FAC84A48D018` = `DarcMissions`
- Esses dois GUIDs novos nao sao a base dos shops, safezone, NPCs, garagem ou helipad. Para trader/safezone, continuar usando `ShopSystem`, `FMShop`, `ZeliksZones`, `British Forces` para helipad e `Placeables/Structures byHeine` para garagem.
- As layers extraidas de `SurvivorZ_693D19523E7D8E79` versao `2.0.17` continuam com o mesmo conteudo principal:
  - `ZTraders.layer`: usa `Prefabs/Clothing.et`, `Juwelier.et`, `Weapons.et`, `Medic.et`, `Vehicle.et`, `Food.et`, `BuildingStuff.et`, `Air.et` e `Drug Dealer.et`.
  - `ZTraderZone.layer`: usa `ZEL_SafeZoneEntity`, garagem `Garage_E_02_base.et` e helipads `E_Helipad_Lights_US_01.et`.
- As layers adicionadas para teste ficam em:
  - `Worlds/chernarus_Layers/shop.layer`
  - `Worlds/chernarus_Layers/safezone.layer`
- Modo escolhido pelo usuario para shops/safezone: opcao 2, sem dependencia do SurvivorZ.
- As layers reais do SurvivorZ extraidas de `data.pak` sao:
  - `Worlds/Chernarus2026_Layers/ZTraders.layer`
  - `Worlds/Chernarus2026_Layers/ZTraderZone.layer`
- O BrasilZ deve copiar a area/posicoes/layout geral, mas sem adicionar a dependencia do SurvivorZ.
- Centro correto da safezone/trader principal do SurvivorZ:
  `<5403.675 299.438 5634.716>`
- A layer `shop.layer` usa as posicoes dos NPCs/traders de `ZTraders.layer`, mas sem `SurvivorZ`, `DZRestored` ou `Mulaz`.
- Nao usar os prefabs de trader externos `Clothing.et`, `Juwelier.et`, `Weapons.et`, `Medic.et`, `Vehicle.et`, `Food.et`, `BuildingStuff.et`, `Air.et` ou `Drug Dealer.et`.
- Depois da remocao do `Mulaz`, cada trader da `shop.layer` deve herdar diretamente de `"{403E2B0BE16D6CC9}Prefabs/ItemShop_Base.et"` do `ShopSystem`; nao criar traders como `$grp GenericEntity` puro, porque o NPC nao aparece.
- Ao configurar um trader herdado de `ItemShop_Base.et`, sobrescrever o componente nativo `ADM_ShopComponent "{5D775155BFB12BA4}"`. Nao adicionar outro `ADM_ShopComponent` com GUID novo, porque a `ADM_ShopAction` nativa fica ligada ao componente original e o botao de loja pode nao aparecer.
- Em `m_Categories`, nao usar alias `$ShopSystem:...`; o ShopSystem carrega essas categorias via `ResourceName` e precisa dos GUIDs reais:
  - Rifles: `"{BD4950A188201509}Configs/ShopCategories/Rifles.conf"`
  - Handguns: `"{06CF3558777CEBF4}Configs/ShopCategories/Handguns.conf"`
  - Magazines: `"{3F39EFFFE7E8E753}Configs/ShopCategories/Magazines.conf"`
  - Grenades: `"{211556D7ACE2140B}Configs/ShopCategories/Grenades.conf"`
  - Launchers: `"{6A0CDFB7108A69E7}Configs/ShopCategories/Launchers.conf"`
- Com apenas `ShopSystem`, os traders ficam funcionais como base, mas os valores/listas ricos de clothing, vehicle, air, food etc. nao sao herdados automaticamente do SurvivorZ/Mulaz. A solucao atual usa `m_AdditionalMerchandise` inline na propria `shop.layer`, com precos de compra/venda BrasilZ-native usando apenas recursos permitidos.
- A layer `safezone.layer` agora usa o centro e a composicao principal de `ZTraderZone.layer`, incluindo pub, castelo, garagem e 3 helipads.
- A `safezone.layer` nao deve usar `ClassFilter` no `ZEL_SafeZoneEntity`, porque esse keyword gerou erro de load no BrasilZ. Tambem nao manter placas/messageboards/walls que pertenciam a recursos removidos junto com o Mulaz.
- A layer `safezone.layer` agora usa o centro e a carcaça principal de `ZTraderZone.layer`, mas somente com recursos encontrados nas dependencias do BrasilZ:
- Nao copiar recursos diretamente do pacote SurvivorZ; quando existir recurso equivalente em uma dependencia permitida, usar esse recurso permitido.
- Nao adicionar `693D19523E7D8E79` (`SurvivorZ`) nem `68DB5125D9B16A0B` (`DZRestored`) ao `addon.gproj`.
- O catalog FM copiado fica em:
  `Configs/EntityCatalog/FMCatalog/InventoryItems_EntityCatalog_Factionless.conf`
- Esse catalog deve ser carregado por apenas um `SCR_EntityCatalogManagerComponent` no mundo. O parent/GameModeSF ja traz um manager; nao adicionar outro manager novo no `Worlds/chernarus_Layers/chernarus.layer` nem em `Prefabs/MP/Modes/BrasilZ/GameMode_BrasilZ.et`. Alem de duplicidade, no Editor isso pode gerar `SCR_EntityCatalogMultiList is not compatible with array of SCR_PlaceableEntitiesRegistry in variable m_Registries`. Se precisar do FMCatalog no GM/loot catalog, mesclar no manager existente em vez de criar um segundo componente, apontando para:
  `"{B70200000000C001}Configs/EntityCatalog/FMCatalog/InventoryItems_EntityCatalog_Factionless.conf"`
- O FM loot spawner decide onde nasce loot pelos slots/spawners (`FM_CatalogLootSlot`/containers), nao apenas pelo catalogo. Colocar item no `InventoryItems_EntityCatalog_Factionless.conf` deixa ele elegivel globalmente para qualquer slot que use esse catalogo/tipo.
- A pedido do usuario, as mochilas raras TriZip/MOTAR e a GPNVG foram removidas do catalogo FM global para nao spawnarem em qualquer lugar:
  - `BZ_TriZip_BagMC.et`
  - `BZ_TriZip_BagBLK.et`
  - `BZ_TriZip_BagCoyote.et`
  - `BZ_MOTAR_CamelBag_70L.et`
  - `BZ_MOTAR_Military_M5.et`
  - `BZ_MOTAR_Tactical_80L.et`
  - `GPNVG_WP_Slotted.et` (nao usar o caminho antigo `GPNVG_Slotted.et`, ele causa risco de GUID/name errado)
- Para o shop de clothing/backpacks, usar os prefabs originais dos mods MOTAR/TriZip, nao wrappers locais `BZ_*`, porque o ShopSystem usa `ADM_Utils.GetPrefabDisplayName(m_sPrefab)` e `GetDisplayEntity()` retorna o proprio `m_sPrefab`; wrappers minimos podem mostrar nome/foto errados no menu.
  - TriZip MC: `{C2F5E3777430D199}Prefabs/Items/Equipment/Backpacks/TriZip BagMC.et`
  - TriZip BLK: `{6063D4580366C999}Prefabs/Items/Equipment/Backpacks/TriZip BagBLK.et`
  - TriZip Coyote: `{52831FD5F93CC7C0}Prefabs/Items/Equipment/Backpacks/TriZip BagCoyote.et`
  - MOTAR CamelBag 70L: `{F01F0A39E7F5CC8B}Prefabs/Items/Equipment/Backpacks/Backpack_MOTAR_CamelBag_70L.et`
  - MOTAR Military M5: `{8F8E05E0E4A91625}Prefabs/Items/Equipment/Backpacks/Backpack_MOTAR_Military_M5.et`
  - MOTAR Tactical 80L: `{63ACC79491813E66}Prefabs/Items/Equipment/Backpacks/Backpack_MOTAR_Tactical_80L.et`
- Esses itens continuam podendo existir em lojas/categorias proprias, mas nao devem ser recolocados no catalogo FM global sem pedido explicito. Para spawnar apenas em base militar, criar/usar catalogo ou slots militares especificos; nao colocar de volta no catalogo factionless global.
- Regra atual das lojas BrasilZ: todo `ADM_ShopMerchandise` compravel pelo jogador (`m_iMaxPurchaseQuantity > 0`) tambem deve poder ser vendido pelo jogador, com `m_iMaxSellQuantity` ativo, `m_bAllowSaleWithFullInventory 1` e `m_SellPayment` em 20% do `m_BuyPayment` arredondado para baixo, minimo 1. Nao aplicar essa regra automaticamente em itens com compra desativada (`m_iMaxPurchaseQuantity 0`), como entradas especiais de venda/recebimento.

## Zombies

- Estado atual escolhido pelo usuario: usar `Ambient Zombies`/`UNDS` em modo ambient natural, estilo DayZ.
- Nao reativar o setup manual do Bacon Zombies no BrasilZ sem o usuario pedir.
- Foram removidos do BrasilZ:
  - `Bacon_622120A5448725E3_GamemodeComponent`
  - `SCR_GameModeSFManager`
  - `Worlds/chernarus_Layers/ai.bacon.layer`
  - `Worlds/chernarus_Layers/zombies.bacon.layer`
- O `addon.gproj` deve manter `692172F6EADE0B44`, que e o `Ambient Zombies`.
- O `addon.gproj` pode manter `622120A5448725E3` como dependencia tecnica de assets/prefabs, mas nao usar GameModeSF/layers/componentes manuais do Bacon.
- A configuracao natural do UNDS fica em:
  `Scripts/Game/BrasilZ/Zombies/BZ_UNDSAmbientConfig.c`
- O loadout dos zombies deve ser controlado por prefab, nao por script de limpeza pos-spawn.
- Prefabs atuais dos zombies do BrasilZ:
  `Prefabs/Characters/Zombies/BrasilZ_Zombie_Civilian_01.et` ate `BrasilZ_Zombie_Civilian_06.et`
- Esses prefabs herdam dos infectados civis do Bacon, mas o `BaseLoadoutManagerComponent` define apenas roupa (`Hat`, `Jacket`, `Pants`, `Boots` quando aplicavel). Nao adicionar `Back`, `Vest`, `ArmoredVest`, armas, mochilas ou itens nesses prefabs.
- Modo atual de teste: esses prefabs continuam sendo spawnados pelo Ambient Zombies. Eles usam o agent compatibilizado do BrasilZ com o behavior/root pronto do Bacon:
  `"{B70100000000A200}Prefabs/AI/BrasilZ_ChimeraAIAgentZombie.et"`
  `"{480A01B2DFD2B5E2}622120A5448725E3/AI/BehaviorTrees/InfectedCharacterRoot.bt"`
- Nao remover a IA customizada do BrasilZ. O behavior custom fica preservado como fallback:
  `"{B70100000000A200}Prefabs/AI/BrasilZ_ChimeraAIAgentZombie.et"`
  `"{B701000000007001}AI/BehaviorTree/Group/BZ_ZombieRoot.bt"`
- A IA customizada usa `BZ_ZombieCharacter.c`. Se voltar para ela, manter `Chase Speed Day` e `Chase Speed Night` em torno de `0.900`; se `Chase Speed Day` voltar para `0.250`, os zombies vao perseguir quase andando durante o dia.
- `Prefabs/AI/BrasilZ_ChimeraAIAgentZombie.et` e uma copia compatibilizada do agent do Bacon: preserva pathfinding/reacoes/tuning do Bacon, mas remove `SCR_AIGoalReaction_Retreat`.
- O `AIPathfindingComponent` desse agent deve usar `NavmeshProject "Soldiers"`, porque o AIWorld do Chernarus precisa carregar `$ChernarusMinusBeta:Worlds/ChernoT/terrainName_soldiers.nmn`. Se voltar para `Zombies`, os zombies podem perceber/atacar perto, mas ficam presos sem andar.
- Nesse agent, manter `AIFormationComponent` e `AISmartActionUserComponent` com `Enabled 0`. Zombies ambient sao entidades solo; se a IA entrar em formacao/smart actions de soldado, eles podem olhar para o jogador e atacar colado, mas nao perseguir.
- Manter `m_EnableTakeCover 0` e `m_EnableCommunication 0` no `SCR_AIConfigComponent`; zombie nao deve procurar cover nem tentar fluxo de comunicacao militar.
- Nao apontar os prefabs diretamente para `BaconZ_SCR_ChimeraAIAgentFull.et` neste projeto: o log mostrou `Unknown class 'SCR_AIDangerReaction_Vehicle'`, `SCR_AIDangerReaction_VehicleHorn` e `SCR_AIGoalReaction_Retreat`. Usar o agent compatibilizado evita esses erros, mantendo o behavior/root do Bacon.
- `BZ_AITestHasTarget` e `AITaskScripted`, nao decorator. Nao colocar `UseChildResult`, `InVariable`, `TestType` ou `TestValue` dentro dele; isso quebra o load do behavior tree com `Unknown keyword/data` e o zombie inicializa sem comportamento completo.
- Zombies nao devem aceitar outros infectados como alvo. `BZ_ZombieCharacter.IsZombieEntity()` bloqueia `BZ_ZombieCharacter` e `Bacon_622120A5448725E3_InfectedCharacter` em:
  - dano recebido
  - alertas de horda
  - `SetCurrentTarget`
  - `TryAttack`
  - `IsTargetValid`
- `BZ_AIDetectPlayer` deve usar `PickClosestValidTarget()` para ignorar infectados vindos do `PerceptionComponent`; se escolher apenas `GetClosestTarget()` bruto, zombies podem detectar outros zombies e brigar entre si.
- Nao registrar `BZ_ZombieCharacter.OnDamageReceived` em `SCR_CharacterDamageManagerComponent.GetOnDamage()`. Ao atirar em zombies, esse hook pode misturar com invokers herdados de Bacon/WCS e gerar erro tipo `ScriptInvoker::Invoke: Incompatible parameter ... UpdateHitZoneState`. A reacao a tiros deve vir por perception/danger events.
- O `BuildPrefabList()` modded do UNDS deve apontar para esses prefabs do BrasilZ, nao diretamente para `Variant_CIV_*` do Bacon.
- O `BuildPrefabList()` deve carregar esses prefabs com `Resource.Load` e inserir em `m_aZombiePrefabsCommon`; se o log mostrar `Prefabs: 0 BrasilZ civilian clothing-only zombies`, nenhum zombie sera spawnado e o problema ainda esta no carregamento da lista, antes da IA.
- Log correto esperado depois do carregamento dos prefabs:
  `[BrasilZ][UNDS] Prefabs: 6 BrasilZ civilian clothing-only zombies`
- Para os zombies se moverem, a layer carregada precisa ter infraestrutura de IA:
  - `SCR_AIWorld BrasilZ_AIWorld_Chernarus` em `Worlds/chernarus_Layers/chernarus.layer`
  - `PerceptionManager BrasilZ_PerceptionManager` em `Worlds/chernarus_Layers/chernarus.layer`
  - o AIWorld precisa sobrescrever os `BaseNavmeshFilesConfig` herdados, usando os IDs internos do prefab base. Criar um novo config com ID novo pode ser ignorado pelo engine e gerar `No navmesh file specified!`.
  - navmesh de soldados:
    `$ChernarusMinusBeta:Worlds/ChernoT/terrainName_soldiers.nmn`
  - navmesh de veiculos:
    `$ChernarusMinusBeta:Worlds/ChernoT/terrainName_BTRlike.nmn`
  - navmesh low-res:
    `$ChernarusMinusBeta:Worlds/ChernoT/terrainName_LowRes.nmn`
- Objetivo dessa configuracao:
  - spawn ambient entre 190m e 330m do jogador
  - o primeiro encontro usa 2 zombies, chance 1.0 e delay inicial curto para facilitar teste
  - log esperado ao carregar a config: `[BrasilZ][UNDS] Ambient zombie tuning loaded: ambient=190-330m chance=1.0 clusters=3/4 seed=2 size=2-3 spread=28 nearbyMax=7 waveOff=true safezoneExclusion=260m`
  - o UNDS bloqueia spawn enquanto o jogador esta a menos de 50m do ponto inicial; para testar, andar pelo menos 60m do spawn e aguardar alguns segundos.
  - grupos ambient maiores, normalmente 2 a 3 zombies
  - limite atual: 3 clusters desejados por jogador, 4 maximos por jogador, 16 clusters globais e ate 7 zombies vivos proximos antes de travar novos spawns
  - ganho de hive baixo
  - wave praticamente desligado
  - cleanup mais distante para nao matar zombie perto demais do jogador
- Atencao: o Ambient Zombies usa prefabs de zombie por baixo. Se o log mostrar `[UNDS] Prefabs: 0 soft, 0 medium, 0 armed, 0 exploder`, falta mod/assets de zombie carregado. Nesse caso, pode ser necessario manter Bacon como dependencia tecnica, mas ainda sem usar layer, GameModeSF ou componente manual do Bacon.
- O `Ambient Zombies` ja depende de `Bacon Zombies` internamente. No BrasilZ, registrar a faccao `BaconZ_Faction.conf` no FactionManager e suficiente para os infectados terem faccao funcional sem recolocar o Bacon como dependencia direta no `addon.gproj`.
