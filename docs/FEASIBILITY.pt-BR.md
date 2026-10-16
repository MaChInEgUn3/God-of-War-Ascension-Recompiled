# Viabilidade de recomp estático — God of War: Ascension (BCUS98232)

Data: 2026-10-13. Fonte dos números: EBOOT.BIN do disco do próprio usuário (dump em servidor local via SSH), decriptado com `rpcs3 --decrypt`; scripts em `D:\GOW A\analysis\`. Itens marcados *(heurística)* ou *(não medido)* não são fatos confirmados.

## 1. Resumo / veredito

**Viável como projeto longo, usando o ps3recomp como base, e só para a campanha single player.** Não é um "recomp puro" estilo N64Recomp/XenonRecomp: é um híbrido — PPU recompilado estaticamente + SPU recompilado por módulo + runtime HLE (SPURS, GCM/RSX → D3D12/Vulkan).

O que torna viável: binário estático e pequeno (6,3 MB de código PPU, 7.556 funções, um único TOC), superfície de firmware pequena (~190 funções no caminho single player), sem NPDRM, e SPU concentrado em middleware Sony conhecido (Edge, ICE BatchJob, MLAA) mais ~10 jobs próprios.

O que pode matar o projeto (em ordem): (1) ~1,5 MB de código SPU em blobs crus que o ps3recomp não identifica hoje; (2) fidelidade do RSX/GCM com command buffers escritos por SPU (Edge/ICE); (3) maturidade do ps3recomp — o próprio README só confirma "roda um jogo recompilado" em Windows.

## 1.1 M0 executado (2026-10-13) — resultados reais

Pipeline do ps3recomp (v0.11.0 no CMake / README 0.14.0) rodado sobre o EBOOT decriptado, saída em `D:\GOW A\work\`:

| Etapa | Resultado |
|---|---|
| `ppu_loader.py` | 7.556 descritores `.opd`, 345 imports/25 libs — **idêntico às minhas medições** |
| `ppu_lifter.py` | **50 s**. Recuperação de limites por prólogo achou **16.336 funções reais** (o `.opd` era só piso); 21.581 com wrappers de entrada no meio de função. 186 MB / 2,6 M linhas de C++. Jump tables: 62 despachantes, 545 alvos |
| Lacunas do lifter | Praticamente só `stvlx` ×63 e `vlogefp` ×8. Os ~15 mil `TODO: .word` são constantes de permute vetorial (`00010203 04050607…`) coladas antes dos prólogos, dados dentro do código, não instruções. 10.129 `bctrl` indiretos vão por `ps3_indirect_call` (lookup em tabela hash em runtime) |
| Compilação (clang-cl 23.1.3 + runtime) | Runtime OK; os 5 blocos lifted **compilam sem erro** (build completo ≈ 7 min, 12 núcleos, ~8 GB de RAM). Único erro foi link (`dxguid.lib` faltando), corrigido com flag |
| Executável | `MyGameRecomp.exe`, 62 MB |
| **Primeiro boot** (VFS só com EBOOT/SFO) | O jogo recompilado **executa o CRT, carrega 15 módulos de firmware via HLE, registra callbacks de sysutil, inicializa SPU/SPURS e cria o primeiro workload SPURS** (`Wws_Job`/`CollisL`, policy module em `0x77CA80`). Primeira falha: leitura/escrita em NULL na função `0x10534` logo no início (provável TLS/estado de init) e 8 NIDs de `cellSpurs` sem handler (`0x9DCBCB5D`, `0x30AA96C4`, `0xC2ACDF43`, `0x4A6465E3`, …) |

**Descoberta SPU (corrige a seção 3):** a biblioteca de jobs é a **WWS Job Manager** da Sony (workloads `Wws_Job`), e a região de 1,5 MB contém **33 módulos de job com marcador `0xC0DEC0DE`** (cabeçalho: 4×`ila`, entry, tamanho, 0, 0, `C0DEC0DE`), todos alinhados a 16 B, soma de extensões ≈ 1,31 MB, mais as 4 imagens ELF. O ps3recomp **já tem tratamento para esse formato** (`tools/extract_jobmods.py`, `wrap_spu_elf.py`, `build_spu_workloads.py` com registro por fingerprint). A lacuna agora é menor: não é "formato desconhecido", é "validar e ligar os 33 módulos".

Scripts do carving: `D:\GOW A\analysis\carve_wws.py`, `spuptrs.py`.

### M0.1 — correções feitas (patch em `D:\GOW A\patches\ps3recomp-gow-m0.patch`, para enviar upstream)
1. **Leitura em NULL em `0x10534`** — era o `_start` do CRT: ele copia `envc+1` entradas de `[envp+4+i*8]`, e o loader do ps3recomp não preenchia `r5` (envp) nem `r6` (envc). Corrigido em `runtime/ppu/ppu_loader.cpp`.
2. **4 NIDs de SPURS v2 sem handler** (`cellSpursInitializeWithAttribute2`, `cellSpursAttributeEnableSystemWorkload`, `_cellSpursTasksetAttribute2Initialize`, `cellSpursCreateTaskset2`) — sem o primeiro, o SPURS nunca inicializava. Adicionados em `libs/spurs/cellSpurs.c/.h` como wrappers das versões v1 (modelo simplificado; `EnableSystemWorkload` só aceita e loga).

**Estado do boot depois das correções:** CRT → 15 módulos de firmware → SPURS instância `"Big"` (2 SPUs) → 3 tasksets v2 → primeiro workload (`Wws_Job`/`CollisL`) → `cellGame BootCheck` → 3 threads `fios` → **backend D3D12 abre a janela** (1280×720, RTX 3080). O processo fica vivo e ocioso (nenhum `open()` de arquivo é chamado).

**Bloqueio atual (exato):** `wid=0 PM NOT LIFTED (fp=0xC8D774963A96DCF2 size=12288) — workload will not run`. O policy module SPU (raw, 12 KB em VA `0x77CA80`) não está lifted/registrado; o runtime o procura por fingerprint FNV-1a-64 dos bytes crus. Provável causa do idle: o job manager WWS espera o SPU do workload.

Observação: `cellGame BootCheck` devolve `titleId='BLES00000'` em vez de `BCUS98232` (não lê o PARAM.SFO do VFS) — a corrigir, pode importar para checagens do jogo.

### Decisão: backend gráfico = **Vulkan** (2026-10-13)
O ps3recomp só oferecia Vulkan em Linux (opt-in, estágio inicial). Portei para Windows (patch no mesmo arquivo): `rsx_vulkan_backend.c` agora carrega `vulkan-1.dll` por `LoadLibrary` (antes `dlopen`), o CMake deixa de abortar em Windows e exige SDL2 (janela/`SDL_Vulkan`) só nesse build, e o `main.cpp` do porte escolhe o backend por `PS3RECOMP_RSX_VULKAN`. Build em `work\portuild_vk` (o `build` D3D12 continua como alternativa); precisa de `SDL2.dll` ao lado do exe. Resultado: `[RSX vulkan] using device 0: NVIDIA GeForce RTX 3080` → `Vulkan backend init OK -- window open`.
**Pendente do Vulkan:** sem `glslang` no build, os shaders do jogo (RSX → HLSL → SPIR-V) ficam indisponíveis — só o caminho fixo. Instalar/compilar glslang e ligar `PS3RECOMP_VK_GUEST_PROGRAMS=1` quando houver draws. O backend é "estágio inicial" no upstream: maturidade menor que o D3D12 (que o README marca como completo).

### M0.2 — PM interpretado + causa do travamento do FIOS (2026-10-13)
- **Policy module SPU agora roda por interpretação** quando não há versão lifted (`spurs_resolve_pm` cai em `spu_pm_interp_entry`; `SPURS_PM_INTERP=0` desliga). O interpretador ganhou as mesmas interceptações de `exitToKernel`/`selectWorkload` do caminho lifted. Resultado: o PM do `Wws_Job` (fp `0xC8D774963A96DCF2`) executa, faz DMA da fila de jobs e sai, em ciclo. Ainda não foi lifted (otimização futura), mas já valida o formato.
- **Causa do idle:** o FIOS embutido (v1.3.5, threads `fios mediathread/scheduler`) girava ~300 mil vezes numa espera de `sys_lwcond_wait` com o aviso `wait for invalid cond 'opWait'` / `'scheduler.m_ioCond'` (o logger do jogo é um stub `li r3,0; blr`, por isso nunca apareceu). O jogo considera a cond válida quando o **word 0** da `sys_lwcond_t` é diferente de zero; o runtime gravava o ponteiro do lwmutex em 64 bits (word 0 = 0) — este binário usa ponteiros de 32 bits. Corrigido em `ppu_sysprx.cpp` (`lwmutex` em +0, id da fila em +4/+8; `sys_lwcond_wait` lê 32 bits).
- **Ferramentas novas de diagnóstico** (no patch): `PS3_ASSERT_TRACE=1` imprime mensagens de assert/log do jogo mesmo com o handler stubbado; `PS3_LWCOND_TRACE=1`; `ps3_sampler_start()` ligado no `main.cpp` (`PS3_SAMPLE=<ms>`).
- **Estado:** com a correção o FIOS inicializa (`FIOS 1.3.5 initialized`), lê `ARCHIVES` e tenta abrir os 19 PSARC em `/dev_bdvd/PS3_GAME/USRDIR/`. Sem os dados no VFS o jogo falha em `openCacheFile` e sai (`sys_prx_exitspawn_with_level`). Copiando os PSARC (~36 GB) para `D:\GOW A\vfs\PS3_GAME\USRDIR\` para continuar.

### M0.3 — dados do disco no VFS: o jogo monta os 19 PSARC (2026-10-13)
Copiei os 19 PSARC (~34 GB) para `D:\GOW A\vfs\PS3_GAME\USRDIR\` (tamanhos idênticos aos do servidor). Correções que isso exigiu (todas no patch):
- **Arquivos > 2 GiB:** `cellFsFstat`/`cellFsStat`/`cellFsLseek` usavam `long`/`stat` de 32 bits (no Windows). `RESOURCES.PSARC` (2,15 GB) devolvia `size=0xFFFFFFFF`, o jogo entrava no caminho de erro fatal e saía. Agora usa `_ftelli64`/`_fseeki64`/`_stat64`.
- **Título errado (`BLES00000`):** o `main.cpp` do porte não chamava `cellGame_init_from_paramsfo`; o jogo monta nomes como `INTRO/BOOT_<TITLEID>.TXT` e caminhos `/dev_hdd0/game/<ID>-DATA`. Agora lê o `PARAM.SFO` (`BCUS98232`). O `main.cpp` do porte está copiado em `D:\GOW A\patches\gow-port-main.cpp`.

**Estado do boot:** CRT → FIOS 1.3.5 → lê `ARCHIVES` → **abre e faz `stat` dos 19 PSARC com sucesso** → sistema DCDS (`dcds_hdd.txt`, procura DLC/patch em `/dev_hdd0`, ENOENT esperado) → continua. Depois disso o jogo fica ocioso: uma thread (tid 7) faz `sys_timer_usleep(500)` em loop e a thread principal não avança.

**Próximo bloqueio (hipótese forte):** o log mostra `[hle] unresolved NID 0x8A85674D` = `_cellSpursLFQueuePushBody`. O runtime tem `_cellSpursLFQueueInitialize` (queue de 12×32 bytes criada com `owner = spurs`) mas **não implementa o Push**; um import sem handler "finge" `CELL_OK` e descarta o dado. O consumidor provável é o **Edge Zlib decompressor** (task SPURS — `edgeDecompressorTaskset` nas strings), que descomprime os blocos dos PSARC. Sem push e sem rodar essa task SPU, nenhuma leitura de PSARC completa. Duas saídas: (a) implementar `LFQueuePushBody` e rodar a task Edge Zlib no interpretador/lifter SPU; (b) **atalho de HLE**: interceptar a task Edge Zlib e fazer o inflate no host com zlib (já previsto na seção 3 como "substituir por código do host").

### M0.4 — Edge Zlib por HLE: os PSARC passam a ser lidos e descomprimidos (2026-10-14)
Decisão tomada (revisão de um segundo avaliador): caminho **(b)**, HLE no push da fila. Implementado `_cellSpursLFQueuePushBody` em `libs/spurs/cellSpurs.c`.
- **Formato real do elemento** (32 B, big-endian), medido no jogo — difere do que eu havia suposto: `{ eaIn, eaOut, compSize, outCapacity, &contador|1, eventFlag, bits<<16, 0 }`. A entrada é **deflate cru** (sem cabeçalho zlib; primeiro byte `0x95` = bloco final dinâmico) e fica **dentro do buffer de saída** (`eaIn + compSize == eaOut + outCap`): descompressão no lugar. O handler copia a entrada para um buffer privado e usa `stbi_zlib_decode_noheader_buffer` (já compilado no runtime via `cellPngDec.c`), aceita zlib com cabeçalho e trata pedidos parciais (`outCap=64`: só um prefixo do bloco). Ao terminar: decremento atômico BE do contador, `ppu_resv_break_store`, e `cellSpursEventFlagSet` quando chega a 0.
- **Resultado:** blocos `18700→65536`, `14021→65536`, `2082→10480`… com 0 falhas; as 5 tasks `CreateTask2` do taskset (barrier de 5 = os workers do Edge Zlib) são aceitas sem escalonar (`cellSpursCreateTask2` / `_cellSpursTaskAttribute2Initialize` como stubs honestos, log em `SPURS_TASK2_LOG=1`).
- Outros handlers adicionados: `cellGcmMapEaIoAddressWithFlags` (sem ele o `Ice::Render` repetia "miooffset == -1" 328 mil vezes), `cellGamePatchCheck` (→ `NOTPATCH`).
- **Estado do boot:** CRT → FIOS → 19 PSARC → DCDS → savedata de perfil → **12 workloads WWS registrados** (`CollisL`, `General`, `General6`, `HighestPrio6`, `ModelServer`, `ModelServerPrep`, `SysMisc`, `BrBDblBufWrkld`, `BatchJobList`, `RenJobSet`×4, `RenderJobs`, `MeshProcJobs`) → `Ice::Render` inicializa o GCM (tiles) → o gerenciador de jobs WWS (interpretado) emite DMAs de lista (`mfc-list STALL`). O processo vive 90 s sem cair.
- **Próximo bloqueio:** o PPU fica em `[HOTREAD16] spinning on 0x200032E4` (espera por algo que o RSX/SPU deveria escrever) e a fila de fences do GCM transborda. Duas hipóteses a testar: (1) o consumidor da FIFO do RSX não está sendo executado; (2) os jobs SPU que preenchem os command buffers (Edge/ICE/RenderJobs) precisam rodar de verdade. Ambas apontam para a camada SPU (gerenciador WWS interpretado + módulos `0xC0DEC0DE`) e para o walker de FIFO do RSX.

### M0.5 — o "spin" em 0x200032E4, o PM lifted e o limite atual (2026-10-15)
- **`0x200032E4` identificado:** é uma entrada da tabela EA→IO do GCM (`HLE_INJECT_BASE+0x3000`, página `0x172` = EA `0x172xxxxx`). O `cellGcmFinish` inline do jogo (`0x3E4858`) calcula `offset = ioTable[ea>>20]<<20 | ea&0xFFFFF` e gira até `ctrl->get == offset`. A FIFO do RSX estava sendo drenada (`get == put`); o problema era o lado do jogo.
- **Bug corrigido (genérico, vai ao upstream):** a cópia da tabela visível ao guest só era atualizada em `cellGcmGetOffsetTable`; mapeamentos posteriores (o `MapMainMemory` de 32 MB) ficavam invisíveis (`0xFFFF`, "iooffset == -1"). Agora cada `populate/clear_offset_table` espelha na memória do guest. Também: `cellGcmMapEaIoAddressWithFlags`, alocador de IO por "menor faixa livre em 256 MB" (antes um bump que nunca reutilizava) e `clear_offset_table` arredondando para cima como o `populate`.
- **Policy module WWS lifted:** extraído de `0x77CA80` (12 KB, base LS `0xA00`), lifted com `spu_lifter.py --auto-functions` (**194 funções**, 4 `.word` de dados) e registrado por fingerprint (`0xC8D774963A96DCF2`, idêntico ao calculado pelo runtime). Gerador reutilizável: `analysis/make_spu_port.py` + `work/spu/images.json`. Resultado: `PM resolved (image=10)`, saída limpa por `exitToKernel` em todos os workloads, interrupções SN (stall-and-notify) entregues; o jogo passa de ~790 para ~890 flips.
- **Peças identificadas:** o `async dispatch MISS size=38100` é a task ELF do Edge Zlib (`0x6A3380`, 4ª das 4 imagens ELF), já atendida por HLE; o consumidor da `LFQueue` e o `CreateTask` v1 de `arg=15E66A80` são ela.
- **Limite atual (duas frentes, ambas dentro do engine do jogo):**
  1. **Handshake de job do WWS:** o PM entra no estágio de carga de job (`mfc-list GETL` + stall-and-notify), a interrupção SN é tomada repetidamente, mas `loadJobState@12A0` permanece `0x00A00001` e o stall nunca é reconhecido. O knob `SPU_SN_DEFER` não altera nada (varrido 0…40). O runtime só foi calibrado para o PM do LBP (versão diferente: as âncoras `0x2BF0/0x2720` não batem; aqui `exitToKernel` está em `0x3738`).
  2. **`Ice::Render` desmonta os mapeamentos IO** logo após o primeiro init (`Unmap io=0`, `Unmap io=0x05200000`) e reclama "MemTable entry not found" na sua própria tabela. A rotina é a reconciliação de mapeamentos em `0x3747B4–0x3749AC` (compara tabela desejada em `0x970F50` com a atual em `0x9811F0`, `Unmap` das divergentes e `MapEaIoAddress` das novas). Preciso entender por que a tabela desejada diverge logo no primeiro ciclo.
- **Frames:** `RSX_TRACE` mostra só `CLEAR_SURFACE` (×11) e nenhum draw; o frame capturado (`PS3RECOMP_VK_DUMP`) é preto. Esperado enquanto os jobs de Edge/ICE, que escrevem os command buffers, não rodam.
- **Descartado:** `SPU_SN_DEFER` (sem efeito); interpretar o PM em vez de lifted (o WWS depende de interrupções que só o caminho lifted entrega).

### M0.6 — firmware instalado e módulos LLE decriptados (2026-10-15)
- Firmware oficial (`PS3UPDAT.PUP`, 196 MB, baixado da página de suporte da Sony) instalado no RPCS3 portátil (`tools\rpcs3\dev_flash`; o erro "Verification failed … process teardown" ao final é inofensivo). Arquivo em `tools\firmware\`.
- **RPCS3 como oráculo:** o jogo inicia (renderer Vulkan na RTX 3080) e roda sem nenhum aviso/erro no log em ~2 min. Para o nosso caso ele é um oráculo **limitado**: usa as bibliotecas do firmware via LLE (`cellGcmSys` etc. vêm de `SPRX Worker`), então não registra as chamadas de GCM como HLE; a primeira execução gasta minutos compilando o PPU/firmware. Também serve de referência de NIDs (ex.: `0x58D58FCF = cellSpursTasksetGetSpursAddress`, ainda sem handler nosso). A pasta de jogo para o RPCS3 é `D:\GOW A\vfs` (com `PS3_DISC.SFB` e `EBOOT.BIN` originais).
- **Passo 1 do LLE concluído:** `libsre.sprx` (SPURS kernel) → `work\lle\libsre.prx` (239 KB) e `liblv2.sprx` → `work\lle\liblv2.prx` (109 KB), ambos `ET_PRX` (`0xFFA4`, PPC64) decriptados com `rpcs3 --decrypt`. Os arquivos do firmware não devem ser distribuídos.
- **Por que isso importa:** `docs/FIRMWARE_LLE.md` do ps3recomp argumenta que o SPURS por HLE "tem sido fonte de problemas em essencialmente todo título SPURS", e que a rota recomendada é lifter e rodar o `libsre` real (é o que o inFamous usa). O bloqueio do WWS (handshake de job, interrupção SN que não avança) é exatamente o tipo de divergência de escalonador que o LLE evita. Próximos passos do pipeline: `lift_prx.py libsre.prx --base 0x02000000` → `ppu_lifter.py --raw` → ligar no runtime (ver o documento).

### M0.7 — `libsre` relocado e lifted (passos 2 e 3 do LLE) (2026-10-15)
- **Passo 2** (`lift_prx.py libsre.prx --base 0x30000000`): módulo `cellSre_Library`, **TOC `0x30039AB0`** (idêntico ao que o runtime do ps3recomp já assume para o `libsre`, o que confirma a convenção de base), imagem de 205.632 B, 1.430 relocações aplicadas, **267 funções exportadas em 6 libs**, 32 imports em 2 libs, 352 sementes de função.
- **Passo 3** (`ppu_lifter.py --raw --base 0x30000000 --symbol-prefix libsre_`): 756 funções, 11,8 s, 479 mil linhas de C++ (51 MB), 26 avisos "no continuation… emitting halt" (todos no limite `0x3002E000`). Artefatos em `work\lle\out\` (imagem, `*_functions/exports/imports.json`) e `work\lle\recomp\`.
- **Achado:** ~97 mil `TODO: .word` e ~9 mil opcodes "desconhecidos" no lift mostram que o `libsre` traz **imagens SPU embutidas** (kernel SPURS e policy modules) como dados em volta do código PPU. Essas imagens precisam ser extraídas e passar pelo `spu_lifter.py` (como fiz com o PM do WWS), e é onde o doc avisa que bugs raros do lifter SPU aparecem ("SPU correctness is a prerequisite").
- **Passo 4 (integração, não feito):** o runtime já tem o mecanismo: `runtime/prx/prx_loader.{c,h}` (`prx_load_module` copia a imagem para a base e registra as funções; `prx_resolve_export(nid)` é o que o resolvedor de imports do jogo consulta antes do stub HLE; ver `ppu_hle.cpp` ~l.323) e o gancho `ps3_load_prx_modules()` do harness. Falta, no nosso porte: (1) compilar `work\lle\recomp` junto, (2) fornecer a tabela `prx_module` (imagem + `libsre_function_table` + exports) e chamar `prx_load_module`, (3) ligar os 32 imports do próprio `libsre` (lv2/sysPrxForUser) às pontes HLE, (4) extrair e lifter as imagens SPU embutidas, (5) deixar de usar o `cellSpurs` HLE para os NIDs que o `libsre` exporta. Risco declarado pelo próprio ps3recomp: validado em um título (Yakuza: Dead Souls), é hipótese aqui.

### M0.8 — passo 4 do LLE: o `libsre` real roda no porte (2026-10-15)
- **Relift correto:** o lift do M0.7 estava inflado (dados lidos como código). Com `--toc 0x30039AB0 --code-end 0x3001D718 --hle-stubs` (os 32 stubs de import viram `ps3_hle_call(nid)`, sem precisar patchear slots) saem **384 funções, 2 MB, 0 avisos** (artefatos em `work\lle\recomp2\`, tabela de exports por `prx_exports.py`). O código PPU do `libsre` termina em `0x3001D718`; de `0x20380` em diante a imagem tem dados SPU.
- **Cola do porte** (`work\port\lle_libsre.cpp`, `lle_keep_hle.inc`, opção CMake `PORT_ENABLE_LLE_LIBSRE`; cópia em `patches\port\`): `ps3_load_prx_modules()` carrega a imagem em `0x30000000`, registra funções e exports; `ppu_hle.cpp` já desvia todo NID exportado para o `libsre` real (`prx_resolve_export`). Modo por env: `GOW_LLE=off|all-hle|real`; `GOW_LLE_FORCE_HLE=<nids>`; `GOW_LLE_IMAGE`; `GOW_LLE_DUMP=<s>` (despeja a `CellSpurs` viva).
- **Escada de validação:** (1) `all-hle` = baseline idêntico (12 workloads, GCM, 12×CLEAR). (2) `real`: o `cellSpursInitializeWithAttribute2` real falhou com `0x80410709` por causa de `sys_process_is_spu_lock_line_reservation_address` (syscall 14) recusar `0x1101AD00` — o runtime só aceitava até `0x10010000`; alarguei até `0x20000000` (`lv2_register.c`). Depois disso o libsre cria `BigCellSpursKernelGroup` (6 threads SPU), event queues, as threads PPU `BigSpursHdlr0/1` e `SPURS SystemWorkload Serv`.
- **SPU embutido no libsre** (extraído de `libsre_image.bin`, lifted com `make_spu_port.py`, que agora aceita `file/foff` e ELFs prontos): kernel SPURS ELF `0x20C00` (2116 B, LS `0x100`, entry `0x848`, fp `0x7093093F295BB518`, imagem 11); PM do system service `0x21480` (imagem 12); PM do taskset `0x23680` (imagem 13). O kernel real roda como "LIFTED SPU execution" nas 6 threads; os PMs entram como overlays residentes via `spu_overlay_register_source(EA, imagem)` (EA 0x77CA80→10, 0x30021480→12, 0x30023680→13).
- **Misto real+HLE não funciona:** com o `CreateTask` HLE o estado do taskset (criado pelo libsre) é rejeitado; e deixar o escalonador real rodar a task Edge Zlib executa código SPU não lifted. Solução atual: a família taskset/task/eventflag/LFQueue/barrier (47 NIDs, `lle_keep_hle.inc`) fica em HLE, o HLE "adota" a instância criada pelo libsre (`g_spurs_lle`, `spurs_inst_find`), e o resto (init, workloads, kernel) é o libsre real.
- **Estado medido:** tabela de workloads real preenchida (wkl[0] = system service `0x30021480`, wkl[1..15] = WWS `0x77CA80`), todos `RUNNABLE` (`state1=2`), `status=0x3F`, `enabled=0xFFFF0000`; só `readyCount[9]=1`. O PM do system service roda e fica em loop `GETLLAR` ocioso. Nenhum WWS é despachado porque o jogo ainda não submete jobs.
- **Conclusão (corrigida após revisão):** o `libsre` real roda e cria/escalona o SPURS, mas **ainda não despacha nada**: `readyCount[9]=1` (BrBDblBufWrkld foi submetido) e nenhuma das 6 threads de kernel o pega; as 5 tasks `CreateTask2` continuam em HLE ("accepted-not-scheduled"). Logo, o sintoma de GCM idêntico (`MemTable entry not found`, spin `0x200032E4`) **ainda não é um teste de independência do SPURS** — o teardown do `Ice::Render` ocorre logo depois dessas 5 tasks e a cadeia "task nunca roda → resultado nunca chega → teardown" não foi descartada. Verificações em curso: (1) por que o `wkl[9]` não é escolhido (prio só na SPU 5, que está presa no PM do system service em loop `GETLLAR`); (2) `maxContention` em `+0x50` (`01 00 .. 01 00 08 08 ..`) contra `maxC=1` do HLE; (3) qual ELF as 5 tasks referenciam e se dá para lifter e rotear só elas ao libsre real.
- **Pista nova (estacionada: o jogo abriu `ARCHIVES` mas nunca `LOCALIZED_ARCHIVES`, os `BOOT*_EN.GTF` são só fallback de arquivo solto):** o jogo tenta abrir `INTRO/BOOT0_EN.GTF`, `BOOT3_EN.GTF` e `BOOT4_EN.GTF` (arquivos soltos que não existem no disco; o dump tem pacotes de idioma `ENGLISH.PSARC`, `FRENCH`, `BR_PORTUGUESE`, `LA_SPANISH` listados em `LOCALIZED_ARCHIVES`, que não estavam entre os 19 copiados). Copiei `ENGLISH.PSARC` (87 MB) para o `vfs`, mas o jogo ainda não o monta; falta descobrir a condição (idioma do sistema?).

### M0.9 — causa raiz provável do bloqueio de GCM: heap `WAD_R_Perm` criado com tamanho 0 (2026-10-16)
- **Ferramentas novas** (todas em `tools\ps3recomp`, patch regenerado): `PPU_TRACE_FUNCS=<hex,...>` no `ppu_lifter.py` (emite `ppu_trace_enter(addr, ctx)` na entrada das funções; reliftar ~35 s + rebuild ~7 min; `PPU_TRACE_MAX`, `PPU_TRACE_DUMP=<fn>,<reg>,<len>` em runtime); `GCM_MEMTABLE=970F50` (despeja a MemTable do jogo e a lista de alocadores a cada map/unmap); `GCM_UNMAP_STACK`/`GCM_UNMAP_SCAN`/`GCM_UNMAP_DUMP` (pilha guest no unmap); `GOW_GUARD=<ea>` (page-guard de escrita); `analysisindstore.py <ea>` (acha leituras/escritas estáticas de um global).
- **Os `twu r1,r1` do jogo são traps (asserts) que o lifter ignora**: o código segue com estado lixo. A primeira mensagem de erro do jogo (`MemTable entry not found`) é a primeira assertion real.
- **Cadeia causal medida** (guest): `func_00047924` (boot: carrega o recurso `R_Perm` via `3CA524`) → `3CA524/3C9FCC` (loop de carga) → cria o sub-heap `"WAD_R_Perm"` com `PushHeap(0x2E208C)` → `Heap::Heap(0x39709C)` com **tamanho 0** (`r4=0`, `lr=0x2E21CC`) → heap com `hi = lo−4`, lista livre corrompida → `Alloc(0x397744)` devolve ponteiro com **+0x10000000** (ex. `0x31B90A00` em vez de `0x21B90A00`) → o `Free` desse ponteiro é roteado ao alocador de páginas mapeadas (`tag 0x2D1E4`, cujo intervalo é 430 MB) → `UnmapMainMemory(ptr, 0)` (`0x375754`) → `MemTable entry not found` / `Cannot unmap MemTable entry 0` (traps ignorados). O tamanho 0 sai de `func_0003AD48(name, &size, &x, 0)` (busca de tamanho do recurso numa tabela de orçamento de memória, hash do nome; globais `0x997834/38`).
- **Descartado:** SPURS/LLE (o sintoma é idêntico com o `libsre` real e com HLE); pool de 512 MB em `0x11000000` vs `0x20000000` (corrigi `sys_mmapper_allocate_address` para first-fit alinhado a 256 MB a partir de `0x20000000`, comportamento real do lv2, e movi a janela de injeção do GCM para `0x70000000` em `stubs.cpp`: o boot não mudou, mantido por fidelidade).
- **Refinamento (mesmo dia):** `3AD48(name,&size,&x,0)` monta a string `"WAD_"+name`, calcula o hash e procura numa tabela cujo ponteiro está em `0x81A6F0`; esse ponteiro é gravado em `0x4479E8`, dentro de `func_00047924`, com o retorno de `339054("WST_Global",0,0)` — uma busca por hash num **registro hierárquico de conjuntos de assets** (`338F28`, até 16 filhos por nó, raiz via objeto em `*(0x9397EC)` com chamada virtual). Se `"WST_Global"` não está registrado, `3AD48` devolve 0 para todo WAD, o heap do `R_Perm` nasce com tamanho 0 e a cadeia acima dispara. Ou seja, **falta algo que o jogo deveria ter registrado/carregado antes de `47924`** (o conjunto "WST_Global", vindo de um WAD/PSARC). **Próximo passo exato:** tracear `338F28`/`338E70` (entradas e quem insere nós no registro) e achar a função que carrega o WST global; verificar se ela depende de leitura de PSARC/`cellFs` que está falhando em silêncio (tamanho/offset) ou de um recurso de idioma (`ENGLISH.PSARC` já copiado, mas não montado).

### M0.10 — o registro de símbolos (DC) nunca é preenchido: o parser do `.dcb` não avança (2026-10-16)
- **`WST_Global` é um símbolo de um DCB.** O hash do jogo (Jenkins one-at-a-time sobre a string em maiúsculas, semente 0, sem finalização; função `0x3C4380`) de `WST_Global` (`0xF10B67E1`) e de `GBL_ProjectSettings` (`0x97A5B426`) estão na tabela de símbolos (pares hash/offset no fim) de `/DC/PS3_BE/WAD_R_FX.DCB` (40384 B, dentro do `GAME.PSARC`). O DCB tem registros com cabeçalho de 0x40 B: tipo `0x0B` (cria o objeto `DC_WAD_R_FX`), tipo `0x0C` (dados, ligam `+0x10`) e tipo `0x0D` (tabela de símbolos, liga `+0x14`).
- **A tabela de handlers** é montada em `0x3CA9A8..` (índice = tipo*4): `+0x2C` = OPD `0x812CE8` (`0x338BC4`, registra o objeto), `+0x30` = `0x812CF0` (`0x338C28`, liga os dados), `+0x34` = `0x812CF8` (`0x338D2C`, liga a tabela de símbolos). O registro é uma lista (sentinela em `0x202389F0`), cada nó aponta para um objeto com `+0x14` = ponteiro da tabela; a busca (`0x338E70`) ignora objetos com `+0x14 == 0`.
- **Medido:** o objeto `DC_WAD_R_FX` é criado (hash `0xDBBC9028`) mas `+0x10/+0x14` ficam 0 para sempre; só o handler `0x338BC4` é chamado (509 vezes), `0x338C28` e `0x338D2C` **nunca**. Em cada iteração do laço `0x3C93E8` o cabeçalho lido em `0x997538` é **o mesmo primeiro registro** (tipo `0x0B`, tamanho 4); o contador `remaining` (`0x99752C`) desce 0x50 por iteração (0x9DC0 → 0), mas o fluxo de leitura (`0x2A8A94` → `0x2A7EF8`, objeto de arquivo em `0x9998AC` com o caminho `/DC/PS3_BE/WAD_R_FX.DCB`) devolve sempre os mesmos bytes: **a posição de leitura do stream não avança**. Nenhum dos 6 `LFQueuePush: inflated` do log é o DCB (13651 -> 40384 B), então a descompressão do DCB não passa pelo Edge Zlib HLE (o caminho de leitura do stream é a hipótese a investigar).
- **Ferramentas novas (env, desligadas por padrão):** `PPU_TRACE_FUNCS=all` no lifter + filtro em tempo de execução `PPU_TRACE_ADDRS=lo-hi,...` (um build só serve para qualquer função); `PPU_TRACE_DUMP=<fn>,<reg|0xEA>,<len>[,<hops>]`; `GOW_PEEK=<ea>,<n>` (loga mudanças de memória do guest a cada 20 ms); `GOW_SCAN=<hex>` (procura bytes na memória do guest); `analysis/psarc_find.py` e `analysis/psarc_extract.py`.
- **Próximo passo:** descobrir por que `0x2A7EF8` (primitiva de leitura do stream) não avança a posição ou relê o início (objeto do handle em `0x20039F30`, flags `+0x108 = 0x40000402`): comparar com a leitura do `DEPENDENCYGRAPH.IFF`, que funciona (3 inflates corretos).

### M0.11 — bloqueio do GCM resolvido: o HLE do Edge Zlib ignorava o deslocamento da fatia (2026-10-16)
- **Correção do M0.10:** o DCB *é* descomprimido pelo Edge Zlib; o log do HLE só mostrava os 6 primeiros inflates (agora `SPURS_ZLOG=1` mostra todos, com as palavras cruas do elemento).
- **Causa raiz:** o FIOS lê uma entrada comprimida do PSARC em fatias. Cada leitura empurra um elemento `{in, out, csz, cap, &counter|1, eventFlag, bits<<16 | skip, remaining<<16}` em que `in` é o bloco comprimido inteiro, `cap` o tamanho da fatia e **`skip` (16 bits baixos de `w6`) o deslocamento dentro do bloco descomprimido**. O HLE ignorava `skip` e devolvia sempre o prefixo do bloco, então o parser do `.dcb` relia o primeiro registro 509 vezes, o registro DC nunca era ligado e daí vinham o heap `WAD_R_Perm` de tamanho 0 e o `MemTable entry not found`.
- **Correção** (`libs/spurs/cellSpurs.c`, `cellSpursLFQueuePushBody`): com `skip != 0` (ou se o bloco não cabe em `cap`), descomprime o bloco inteiro num cache por thread de uma entrada e copia `[skip, skip+cap)`.
- **Resultado medido:** os handlers `0x338C28`/`0x338D2C` rodam, `WST_Global`/`GBL_ProjectSettings` resolvem, zero `MemTable entry not found`. O boot avança: troféus (`sceNpTrophy`), savedata (`ListAutoLoad`), callbacks de sysutil/GCM, `SetFlipMode(2)`, `ENGLISH.PSARC` aberto, e o jogo começa a submeter jobs WWS (`ReadyCountStore`, listas DMA com stall-and-notify em `loadJobState@0x12A0`).
- **Novo ponto de parada:** a thread principal espera em `0x20A9AE94` (lr `0x467B2C`); provavelmente pela conclusão dos jobs WWS no SPU (o handshake da lista DMA com stall estacionado no M0.8). Também há 16 leituras de NULL+0x40 em `0x37DE80` e 2 NIDs sem resolver (`0xA73BF47E`, `0xC765B995`).

### M0.12 — jobs WWS executando no SPU; SPURS com 32 workloads (2026-10-16)
- **Handshake do job manager WWS resolvido** (stall-and-notify das listas DMA): o handler de interrupção do PM (LS `0xA2C`) **gera código** (um shim que salva/restaura 80 registradores em `0x3FEC0`) com `lqa`/`stqx`/`stqa` e o chama. Três bugs do runtime impediam isso:
  1. `spu_smc_microstep` calculava o endereço de `stqa`/`lqa` com `<< 4` (o ISA é `I16 << 2`): o template era lido de `0x2900` em vez de `0xA40` e o shim saía lixo — inclusive um `bisled` com bit E que reabilitava interrupções e as aninhava infinitamente. (Achado do advisor.)
  2. Quando o interpretador devolvia o controle a código liftado, o microstep não rearmava o trampolim: a execução do PM terminava no meio do handler (`[pm-end] pc=0xA60`).
  3. O interpretador não tinha `bisled`, os bits E/D dos desvios indiretos nem as operações de dupla precisão (`dfa/dfs/dfm/dfma/dfms/dfnma/dfnms/dfc*`), `bgx`, `mpyhhau`, `mpyui`, `rothmi`, `rotmah(i)`; e `mpyhha` gravava em `rc` (é RR, destino `rt`). Os jobs WWS (módulos de job não liftados) rodam pelo interpretador.
- **Resultado:** STALL → TAKE → RESUME → IRET funcionando, `loadJobState` avança, milhares de jobs executados, a espera do PPU em `0x466E38` (mínimo dos contadores de jobs por SPU) é liberada.
- **SPURS com 32 workloads:** o jogo usa `cellSpursInitializeWithAttribute2` (`CellSpurs2`, 8 KB) e registra **19** workloads; o HLE tinha 16 slots (e `spurs_policy.c` também), então `EDGE2` e `RsxSync` nunca eram registrados e o contador de conclusão de jobs em `0x99AAC4` nunca zerava. Implementado o segundo conjunto (wid 16..31) com o layout real: readyCount2 em `0x10`, state2 em `0xC0`, signal2 em `0x74`, wklInfo2 em `0x1000`, contenção em nibbles.
- **Novo ponto de parada:** um SPU (PM WWS, img 10) gira em `GETLLAR ea=0` em LS `0x4194`, e o RSX recebe um `GET_REPORT` com índice `1048575`; a thread principal espera um job set de renderização (`0x21600530`). Suspeita: um job com EA nulo (sincronização RSX/report).
- **Ferramentas:** `SPU_IRQTRACE=1` agora reporta interrupções aninhadas com o shim gerado; `SPURS_WID_STATS=1` conta execuções por workload.

### M0.13 — layout do CellSpurs2 corrigido; EDGE e RsxSync agendados (2026-10-16)
- **NIDs sem implementação resolvidos:** `0xA73BF47E = _cellSpursWorkloadFlagReceiver`, `0xC765B995 = cellSpursGetWorkloadFlag` (calculado com `tools/nid_database.compute_nid`). Implementados: `GetWorkloadFlag` devolve `spurs+0x60` (`CellSpursWorkloadFlag`, palavra viva em `+0x6C`), `WorkloadFlagReceiver` grava o receptor (wid 18 = RsxSync).
- **Layout real do cabeçalho do `CellSpurs`** (confirmado pelo código do job WWS): `0x72 sysSrvMessage`, `0x73 spuIdling`, **`0x74 flags1`** (`SF1_32_WORKLOADS = 0x40`), `0x76 nSpus`, **`0x77 wklFlagReceiver`**, **`0x78 wklSignal2`**. O HLE agora liga `SF1_32_WORKLOADS` no `CellSpurs2` e copia a primeira linha do `CellSpurs` para o `tempArea` do contexto do kernel (LS `0x100`), como o kernel real faz com GETLLAR na seleção de workload. Sem isso, o job que resolve o `wklInfo` do receptor (wid >= 16) devolvia EA 0 e girava em `GETLLAR(0)`.
- **Resultado:** acabou o spin em EA 0; os 19 workloads rodam (dezenas de milhares de execuções de PM); o contador de conclusão do EDGE2 vai de 1 para 0.
- **Novo ponto de parada:** no lote seguinte, os contadores de EDGE0 (wid 15, `0x99AAC8`) e EDGE1 (wid 16, `0x99AAC0`) ficam em 1; a thread principal espera em `0x3E7A14`. Ainda só 2 flips: o RSX quase não recebe comandos.

### M0.14 — formato das job lists WWS e a cadeia de sincronização EDGE/RsxSync (2026-10-16)
- **Entradas de job list** (8 bytes, `{u32 cmd, u32 arg}`): `cmd` low-halfword 1 = job (`0x00A00001`/`0x00600001`, arg = EA do job), **2 = decrementar o contador de dependência em `arg`**, **5 = esperar o contador em `arg` zerar**, 3 = nop. Adicionadas pelo PPU via `0x467BE0`/`0x46757C` e família (`0x467984` = espera).
- **Contadores de dependência:** `u32 (count<<16) | (wid<<8) | flags`; quando chegam a 0 o PM acorda o workload `wid` escrevendo o readyCount dele no `CellSpurs` (atômicos no PM em `0x397C`, chamados de `0x2698`/`0x311C`). Medido em `0x99AA80`: barreira `0x99AAA4` (5 → 0) acorda EDGE1 → EDGE2 → EDGE0; C4 (EDGE2) zera por comando tipo 2.
- **Estruturas de workload WWS (data):** `[2]` índice da lista corrente, `[4..7]`/`[8..11]` por lista, `[12..15]` EAs das 4 job lists, `+0x40` linha de tickets `(publicados<<16 | consumidos)`. RsxSync (wid 18): 4 listas em `0x23E84180..`, tickets `0x000D0001` → **parado na entrada 1**, `(5, 0x2001ED1C)`.
- **`0x2001ED1C`** é um contador criado por `func_003B3028` (marca `count=1` e acrescenta a espera ao RsxSync); IO = `0x0001ED1C` (EA `0x20000000` ↔ IO 0). **Nenhuma entrada tipo 2 e nenhum comando RSX na memória referenciam esse contador** (GOW_SCAN), então quem deveria zerá-lo ainda não apareceu — candidato: comando de semáforo do RSX emitido mais tarde no frame (o FIFO está drenado: put=get=`0xA80000`), ou um caminho do PPU que não roda porque a thread principal espera o EDGE0.
- **Próximo passo:** achar quem libera os marcadores do RsxSync no hardware real (provável: RSX via `NV406E`/`NV4097` semaphore com contexto DMA de memória principal, ainda não implementado no HLE) e verificar se a espera da thread principal (`0x3E7A14`, contador EDGE0) é anterior a isso no frame. RPCS3 como oráculo para os valores em `0x2001ED1C` ao longo do tempo.

### M0.15 — reports do RSX em memória principal liberam o RsxSync (2026-10-16)
- **Quem zera os contadores de espera do RsxSync é o RSX**, via `NV4097_GET_REPORT` (`0x1800`): com `NV4097_SET_CONTEXT_DMA_REPORT` (`0x1A8`) = `0xFEED0001` (host buffer = memória principal via IO), o report `{u64 timer, u32 value, u32 0}` cai no EA mapeado do offset. GoW:A aponta reports para os contadores WWS (`idx 7889` → `0x2001ED10`, o campo zero em `+0xC` é o contador) e para `spurs+0x60` (zera `wklFlag.flag`, o "kick" de hardware do RsxSync). O HLE gravava só numa área de reports local do host.
- **Correção** (`cellGcmSys.c`): rastreia `0x1A8`; para `0xFEED0001`/`0xBAD68000` grava o report no EA (`gcm_io2ea`).
- **Resultado:** o RsxSync avança (tickets consumidos 1 → 4); os marcadores são liberados.
- **Novo ponto de parada:** BRANCH-TO-0 num job do RsxSync em LS `0x4264` (lr `0x4258`, módulo de job carregado em `0x4020`, VA = LS + `0x778180`); os contadores EDGE0/EDGE1 continuam em 1.

### Próximos passos imediatos
0b. **Frente atual:** Ice::Render/GCM (`MemTable entry not found`) e o motivo de o jogo não montar `ENGLISH.PSARC`; depois testar o handshake WWS sob o kernel real.
0a. (feito, ver M0.8) **SPURS por LLE** (passos 2–4 de `FIRMWARE_LLE.md`) com `libsre.prx`/`liblv2.prx` decriptados.
0. **Duas frentes, em ordem de custo:** (a) engenharia reversa da reconciliação de mapeamentos do `Ice::Render` (`0x3747B4`) para saber por que ela desmonta o IO; (b) depurar o handshake de job WWS do PM lifted (qual valor o handler de SN espera em `loadJobState`/`jobHeader`; comparar com o fluxo documentado no LBP). Um oráculo seria o RPCS3 com firmware (a ferramenta já está em `tools/rpcs3`), útil para comparar traces de SPU.
1. **SPU:** extrair o PM de `0x77CA80` (e os demais PMs/workloads que o jogo registrar), embrulhar (`wrap_spu_elf.py`), lift (`spu_lifter.py`) e registrar por fingerprint dos bytes crus; depois os 33 módulos WWS. Atenção: a infra SPU do ps3recomp é majoritariamente por título (LBP/YDKJ/flOw) — a camada de WWS Job Manager provavelmente exige código novo.
2. Corrigir o título lido pelo `cellGame` (PARAM.SFO).
3. Copiar os PSARC pequenos (`GAME`, `SHELL`, `bin`) para o VFS local e observar quais arquivos o jogo abre.

## 2. Medido no binário

| Item | Valor |
|---|---|
| Formato | ELF64 BE PPC64, ET_EXEC, **estático**, sem PRX do jogo; SELF key rev `0x1c`, SDK 4.31, sem NPDRM |
| `.text` PPU | VA `0x10230`, 0x617710 B = **6,3 MB ≈ 1,6 M instruções** |
| Funções | **7.556 entradas no `.opd`**, todas dentro do `.text`, **um único TOC (`0x81ef60`)**. É um piso, não contagem exata |
| Dados | `.data`/`.bss` ≈ 2 MB (memsz 0x2021b8) |
| Imports de firmware | **25 módulos / 345 funções** |
| Código SPU | **seção VA `0x681880`, 1,51 MB (0x171b00)** — 0 instruções SPU-exatas no `.text` PPU contra 2.134 `bi $lr`, 955 `stqd $lr`, 5.842 `lnop` nessa seção. **≈ 2.100+ funções SPU** *(contagem por padrões exatos de opcode; é piso)* |
| Imagens SPU em ELF | 4 (176 KB, todas com LS base `0x3000`, entry `0x3050`/`0x3070`), no início da seção; o restante (~1,3 MB) são blobs sem cabeçalho ELF |
| WADs/PSARC | Nenhuma imagem SPU em ELF em `GAME`, `SHELL`, `HECA`, `RESOURCES`; o código SPU mora no EBOOT. Dados: 36 PSARC, ~36,5 GB; MP ≈ 4 GB |

### Imports (single player vs online)
- Online / stub-áveis (≈146): `sys_net` 23, `cellNetCtl` 16, `sceNp*` 62, `cellHttp*` 20, `cellSsl` 7, `cellVoice` 15, `NpEula` 3. Trophies: 11.
- **Núcleo single player (≈188 funções, 12 módulos):** `sysPrxForUser` 30, `cellSpurs` 45, `cellGcmSys` 29, `sys_fs` 26, `cellSysutil` 19, `cellGame` 8, `libvdec` 8, `sys_io` 7, `cellAudio` 7, `cellSysmodule` 5, `cellRtc` 2, `AvconfExt` 2.
- `cellSpurs` (NIDs resolvidos pela base do ps3recomp, 44 de 45): **Taskset2 + `AddWorkloadWithAttribute` + LFQueue + EventFlag + Barrier**; **não** usa `JobChain` nem `JobQueue`. Ou seja, o jogo registra *workloads* próprios (policy modules) e tasksets — o caminho SPURS mais difícil de emular, mas é o que o ps3recomp já descreve (kernel/policy/job por `image_id`).
- `libvdec` (`StartSeq/DecodeAu/GetPicture/...`) + `EXTRASCIN2.PSARC` 9,3 GB: cutscenes por decoder de vídeo do firmware. O ps3recomp marca media decode como parcial → lacuna conhecida e adiável (dá para trocar por ffmpeg).
- Áudio: só `cellAudio` (7 funções), sem `cellAdec`: mixagem/decodificação em software (PPU/SPU).

## 3. Bibliotecas SPU identificadas pelas strings do EBOOT

| Pilha | Evidência | Estratégia |
|---|---|---|
| **Sony Edge Zlib** | `EdgeZlibDecompressor`, `edgeDecompressorTaskset` | trocar por zlib do host (o ps3recomp já trata Edge zlib como middleware comum) |
| **MLAA SPU** | `SPUMLAATask`, `fxMLAAStart/End` | substituir por shader/host ou lift pontual |
| **Edge Geometry** | `EDGE0..2`, `Edge_ZNew/VNew/Shad/Opaq/Stall`, `EdgeWatchdog`, `PS3_UseEdgeLighting` | substituir (semântica pública) ou lift |
| **ICE BatchJob** (Naughty Dog / SCE) | `ice*batchjob*.inl`, `Ice::BatchJob::*` | lift do kernel uma vez; jobs são dados |
| Jobs próprios (~10) | `SPUShadows4`, `SPUMeshHierarchy`, `SPUGroupVis`, `SPUPrimBuilder`, `RenderJobs`, `MeshProcJob(s)`, `RenJobNewMesh`, `WaterProcJob`, `ParticleWaitForSPUJobs` | lift por módulo — é o núcleo do trabalho |
| `SPUReadPackets/WritePackets` | rede | cortado junto com MP |

Referência: GDC 2009 "Practical SPU Usage in God of War 3" (Santa Monica) descreve loader residente de ~2 KB nos local stores; notas de terceiros, só indicativo para o Ascension.

## 4. Estado da arte

- **ps3recomp** (`sp00nznet/ps3recomp`, v0.14.0, out/2026, 1.611 commits): lifters PPU e SPU, extração de imagens SPU, ~93 módulos HLE completos (inclui `cellSpurs`/`SpursJq`, `cellGcmSys`), processador RSX com backend D3D12 que roda os shaders reais, Vulkan/Metal em evolução, LLE opcional do `libsre` (SPURS kernel) com `dev_flash` do usuário. Portes relatados por terceiros (não verificados por mim): inFamous (~13 fps), Killzone 3 (fase 1), Twisted Metal, God of War II HD.
- A doc do ps3recomp (`PROTO_BUILDS.md`) já conclui que "a maior parte do SPU é middleware pré-compilado" e usa fingerprints do `.text` SPU para identificar blobs. **Não encontrei menção a ICE BatchJob nem a Edge Geometry** — provavelmente a maior lacuna do ps3recomp para este título.

## 5. Riscos e perguntas em aberto

1. **Blobs SPU crus (~1,3 MB):** precisam ser delimitados (cabeçalhos de job, tabelas de ponteiros) antes de qualquer lift. Não medido: número de módulos distintos.
2. **RSX:** Edge/ICE escrevem command buffer do RSX direto do SPU; há handshake PPU↔RSX (`PPU_RSX_Callback`, `RsxSync`, `RsxWakeup`). O walker de FIFO do ps3recomp é um bom sinal, mas é o ponto mais provável de bugs visuais.
3. **Tempo real:** o jogo foi balanceado para 6 SPUs; a versão recompilada precisa de paralelismo real de SPU (threads) para fechar 30 fps.
4. **Vídeo e áudio:** cutscenes (`libvdec`) e mixer próprio — escopo separado.
5. **Dados:** formatos `.wad`/`.pak`/`.dci` e PSARC são lidos pelo próprio jogo; não é preciso converter.
6. Legal: usar apenas o dump do disco do próprio usuário; não distribuir binários/assets nem as chaves.

## 6. Plano sugerido (marcos)

1. **M0** — rodar o pipeline do ps3recomp (stage 1: imagem/OPD/imports; stage 2: lift PPU) sobre o EBOOT e medir o que quebra.
2. **M1** — boot até o menu com HLE stub: `sysPrxForUser`, `sys_fs`, `cellSysutil`, `cellGcmSys`; online e NP em stub.
3. **M2** — demarcar e catalogar os blobs SPU (fingerprint + tabela de jobs); ligar Edge zlib ao host.
4. **M3** — lift dos jobs próprios + ICE BatchJob; SPURS workload/taskset funcional.
5. **M4** — Edge Geometry/MLAA (substituir ou lift); validar frame a frame contra RPCS3 como oráculo.
6. **M5** — cutscenes (`libvdec` → ffmpeg), áudio, desempenho.

Escopo cortado: multiplayer (`MULTIPLAYER`, `MP-*`, `SPUReadPackets/WritePackets`), `PS3_EXTRA`, `TLOUCOUNTDOWN.BIN`.

## 7. Artefatos locais

- `D:\GOW A\dump\` — EBOOT.BIN, EBOOT_enc.elf (decriptado), PARAM.SFO (cópias do disco do usuário; não versionar/distribuir).
- `D:\GOW A\tools\rpcs3\` — RPCS3 portátil (usado só para `--decrypt`); `D:\GOW A\tools\ps3recomp\` — clone raso de referência.
- `D:\GOW A\analysis\` — scripts de medição (SELF/ELF, imports, opcodes SPU, PSARC).
