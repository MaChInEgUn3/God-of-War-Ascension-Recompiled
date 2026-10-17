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

### M0.16 — tarefas do Task2, park do FIFO e RsxSync completo (2026-10-16)
- **O BRANCH-TO-0 era um assert do job** (`stopd` em LS `0x4260`). O módulo staged em `0x4020` é outro (VA `0x70C1A0`; desmontar a partir do dump de LS, não do ELF). A função em `0x4888` é o `cellSpursSendSignal` do lado SPU (erros `0x804109xx`): faz GETLLAR do cabeçalho do taskset e, como o bitset `enabled` estava zerado, devolvia `0x80410905`.
- **Causa:** o HLE de `cellSpursCreateTask2` tinha a assinatura errada (3 args). A real é `(taskset, id*, eaElf, argument*, attr2*)`: `attr2` = `{rev, sizeContext, eaContext u64, lsPattern[16], name}`. Ele lia `eaElf` como atributo e nunca registrava a tarefa. GoW:A cria 5 tarefas do ELF `0x68F580` (39.508 B; taskset `0x20027280`; arg `{0x9BAC30, i}`; barreira `0x9BAC80`, total 5). É o mesmo subsistema dos jobs do RsxSync, provavelmente pós-processamento (MLAA). Não é o Edge Zlib, que é o ELF `0x6A3380` via `CreateTask`.
- **Correção:** `CreateTask2` repassa para `cellSpursCreateTask` (bitsets, TaskInfo, dispatch assíncrono). O taskset HLE ganha um wid próprio, alocado de 31 para baixo, porque o `SendSignal` do SPU dá kick no `wid` do taskset. O ELF das tarefas foi liftado como imagem 14 (`spu/images.json`). As 5 tarefas agora rodam: WAIT_SIGNAL → trabalho → barreira → WAIT_SIGNAL.
- **Park do FIFO:** o GoW:A estaciona o RSX num JUMP para si mesmo (`0x2810D8`) e escreve o próximo segmento logo depois; a thread principal reescreve o park mais tarde. As heurísticas `jump-park` e `stuck-get` do walker (vindas de Twisted Metal/VF5) pulavam para `put` e perdiam os `GET_REPORT` seguintes (por exemplo, o que libera `0x2001ED2C`). Com `GCM_PARK_WAIT=1` (padrão no port) o walker espera o patch. Resultado: 8 reports em memória principal, e o RsxSync consome a lista inteira (`0x00120012`).
- **Semântica revista:** a espera da thread principal (`func_003E799C`) é por slot de frame (`idx = (frame+2)%3`): primeiro `0x99AAC0+idx*4`, depois os contadores de lote `0x99AA80+i*4` (EDGE0 `0x99AA88` = 5 jobs, EDGE1 `0x99AA8C` = 5, EDGE2 `0x99AA90` = 6). O cmd `2` da lista **não** decrementa na hora: a lista do EDGE0 consumiu `(2,0x99AA88)` e o job `0x300001 @0x2204ED00` e o contador continuou em 5. Logo, o decremento acontece quando os jobs anteriores terminam (fase store).
- **Novo ponto de parada:** os jobs EDGE entram na pipeline da PM e não concluem; os contadores de lote ficam em 5/5/6. Só 2 flips.

### M0.17 — EDGE destravado: ACQUIRE do RSX + HLE do MLAA (2026-10-16)
- **O EDGE0 não estava travado na pipeline.** A PM lê a entrada `(2, 0x99AA88)` e o contador (`SPURS_PM_DMA_WID=15`) e cede a vez enquanto a contagem não é 0. O cmd `2` é uma espera.
- **Quem zera o contador:** 1 decremento de um job do RsxSync (modo 2, `0x9BCD00`) mais 5 das tarefas de MLAA (uma por tarefa). O lote é descrito por um bloco de controle único em `0x9BAC30`: `{lote, labelEA (0x70000520/0x510 alternados), contadorEA (0x99AA84 + 4·n), buffer 0x20AB0000}`. Cada tarefa grava `label = lote` e decrementa o contador; o decremento que chega a 0 grava `0x0000FF00` e dá kick no wid do contador (um EDGE).
- **O RSX espera essas tarefas** com `NV406E SEMAPHORE_ACQUIRE` (label 0x510 == 2, ...). O walker ignorava o ACQUIRE e corria à frente: `GCM_SEMA_ACQUIRE=1` (padrão no port) passou a bloquear. O detector `stuck-get` agora respeita um ACQUIRE bloqueado.
- **Corrida restante:** liftadas, as tarefas levam cerca de 1,7 s por lote (contra poucos ms no hardware). Medido: 0 timeouts de espera, 0 espera de lock, 0 interpretador; `cpu ≈ 0,7·wall`, ou seja, o código SPU liftado é cerca de 100× mais lento (um trampolim por bloco básico, registradores em memória). Com isso o RsxSync posta o lote N+1 no bloco único enquanto o N ainda roda, e um sinal é consumido pela espera da barreira. A ordem no FIFO não dá garantia: não há ACQUIRE entre o post 1 e o post 2.
- **Decisão: HLE do taskset de MLAA** (`port/gow_mlaa_hle.c`), atendendo cada lote no instante do sinal. Um gancho de título (`g_spu_line_commit_title_hook`) vê o PUTLLC do job no bitset `signalled` do taskset e, para cada bit de tarefa, grava o label e decrementa o contador (com kick ao chegar em 0). As tarefas só ficam estacionadas. O quadro fica sem anti-aliasing; `GOW_MLAA=lifted` volta às tarefas reais. Os efeitos externos foram medidos antes (`SPU_DMA_IMG=14`): framebuffer, buffers internos em `0x20E0xxxx`, barreira e linha `0x9BAD00` são internos; os únicos efeitos externos são label, contador e kick.
- **Resultado:** os lotes fluem (29 em 28 s), os workloads EDGE0/1/2 (wids 15/16/17) passam a entrar em jobs e os contadores por slot zeram.
- **Novo ponto de parada:** `func_00397328` em loop lendo NULL+0/NULL+4 (lista com nó nulo); continuam só 2 flips.

### M0.18 — bug do lifter PPU (spill tomado por save de prólogo) e PM WWS presa em tag com stall (2026-10-16)
- `func_00397328` é a busca na free list de um heap (lista circular: tamanho em +0, next em +4). Com `GOW_HEAPWALK`, o único nó livre do heap `0x23FB8F68` (em `0x240C18F4`) é zerado. O guard aponta para `memcpy` (`0x4EB714`) chamado por `func_0037D558`, num buffer dimensionado por `func_0037DE80`. Essa é a mesma função das leituras **NULL+0x40**, pendentes desde a M0.12.
- **Causa: bug do `ppu_lifter.py`.** A otimização de restauração de callee-saved só aceita um `std rN,X(r1)` como "save de prólogo" se ele vier antes da primeira redefinição de rN. Mas `_first_def` só reconhecia atribuições no início da linha, e `addic`/`addc`/`subfc` são emitidos dentro de um bloco `{ ... ctx->gpr[N] = _r; }`. O spill intermediário `std r24,0x78(r1)` virou "save", e o reload `ld r24,0x78(r1)` virou `_cs_24` (o r24 de entrada, 0). Resultado: o tamanho foi calculado sobre NULL+0x40, o buffer ficou pequeno e o `memcpy` estourou sobre o heap.
- **Correção:** `_first_def` passa a reconhecer a atribuição em qualquer ponto da linha. São 18 funções afetadas no relift (`0x37DE80`, `0x383344`, `0x395C24`, `0x3EBC30`, `0x4318F0`, ...). Resultado: zero null-reads e heap íntegro durante toda a execução.
- **Novo ponto de parada:** a thread principal espera um workload ficar ocioso (`cellSpursGetWorkloadInfo`: `readyCount==0 && contention==0`; `SPURS_WINFO_LOG`), mas a execução da PM do wid 7 (RenJobSet) não retorna (`SPURS_PM_HANG=3`). Ela fica no laço de espera de tag (LS `0x2350–0x2388`: janela de interrupção `bi`+E/D, depois `WrTagUpdate`/`RdTagStat` com máscara `0x20`). A tag 5 é uma lista DMA de 2 elementos em LS `0x1580`, com stall-and-notify. O SN está pendente (`evstat&evmask=0x2`, `stallstat=0x20`), a interrupção foi tomada uma vez com `srr0=0x2358`, mas o handler não deu ACK no stall da tag 5. Falta entender o que o handler espera para a tag 5 (em contraste com a tag 0, que é a carga de job).

### M0.19 — vetor de interrupção da PM sobrescrito por um job (2026-10-16, diagnóstico)
- O SN pendente nunca é atendido porque o vetor em LS 0 deixou de ser um branch (`[spu-int] ... LS0 word 0x00000280 is no branch -- not taken`).
- **Quem sobrescreve** (`SPU_LIST_TAG`, detector de mudança em LS 0): um `PUTL` (cmd 0x24, tag 31) do job do wid 7 em LS `0xDB1C`. O job monta o descritor de saída `(tag<<24) | listLSA` em LS `0xE9F0` (código em `0xD708–0xD74C`) a partir do retorno de uma chamada à JobApi da PM (`bisl` em `0xD714`, com retorno visto em `lr=0x16F0`, `r4=2`, `r6=1`). Essa chamada devolveu `r3 = 0xFFFFFFFF` (falha), o descritor virou `0xFF000000` (lista em LS 0) e o elemento real (`size 0x280 → EA 0x240810B0`, em LS `0xEA30`) foi copiado sobre o vetor.
- **Próximo passo:** descobrir por que a função da PM que aloca buffer/tag (JobApi perto de `0x16F0`; conferir junto `logicalToBufferNumArray @0x11B0` e `bufferSetArray @0xDF0`) falha para esse job do RenJobSet. A causa provável é o binding de buffer-set não feito no carregamento do job, ou outra instrução mal emulada no interpretador.
- Medido depois: na entrada do job, `runJobNum@0x12E0 = 0` e `loadJobNum@0x12D0 = -1`, iguais aos de workloads saudáveis, então não é o caso de `runJobNum = -1` visto no LBP. A tabela de API do job tem `0x16F0` (espera de tag 3) na entrada 0. A chamada que falha é a de lote de buffers `0x1760` (`r4 = 2` requisições → `0x2FD8` GetBufferTag → `0x2EA0` GetLogicalBuffer). O próximo passo é ler o cabeçalho de buffers desse job e o que `0x2EA0` exige.

### M0.20 — `bisl $0,$0` no interpretador SPU: wid 7 destravado, ~740 flips (2026-10-16)
- **Causa raiz do M0.19.** A tabela de API do job (LS `0x1370`) é `{0x16F0, 0x2FD8, 0x1760, ...}`. O job roda `rotqbyi $0,$0,4; bisl $0,$0`, uma chamada indireta para `0x2FD8` (GetBufferTag com `set=2`, `logical=0`, `useTag=1`) em que o registrador de link é o mesmo do alvo. O interpretador (`spu_interp.c`, `SPU_bisl`) gravava o link antes de ler o alvo, então a chamada "retornava" à instrução seguinte sem executar. O job usava então o `r3 = {-1,0,0,0}` deixado pela API `0x16F0` (`0x2350` devolve `ceq(tagstat, mask)`): tag `0xFF` e endereço LS 0.
- Pela análise estática com o dump da LS, o resultado correto era o buffer 4 do set 2 (`bufferSetArray @0xDF0` = `D8108204`, página 216 → LS `0x36000`), com a tag alocada por `0x2930`.
- **Correção:** no `bisl` o alvo é lido antes de gravar o link, no interpretador e também no `spu_lifter.py` (`bisl`/`bisled`). Nenhuma imagem liftada atual tem `rt == ra`, então não é preciso reliftar. O microstepper (`spu_channels.c`) já fazia na ordem certa.
- **Resultado:** `SPURS_PM_HANG=3` não reporta nada, o wid 7 chega a contention 0, e a contagem vai de 2 para cerca de 740 flips em 48 s (`GCM_FLIPCOUNT=1`, ~15 por segundo). A MLAA atende até o lote 12.
- **Nova parada (~48 s):** os flips cessam e a thread principal gira em `[HOTREAD] 0x20A9AE94` (lr `0x467B2C`), a mesma espera de conclusão de job WWS da M0.10.

### M0.21 — as paradas de ~50 s ficam no lado do RSX, não nos jobs (2026-10-17)
- O `HOTREAD 0x20A9AE94` é só a espera normal, a cada frame, da lista de jobs de `Wws_Job/HighestPrio6` (data `0x20A9AF00`; `0x466E38` compara o mínimo dos contadores por SPU em `+0x40+16*idx` com o fim da lista). A lista segue sendo preenchida e concluída depois do último flip.
- **Parada 1, o thread do frame clock preso no decode de textura.** Com `GOW_CLOCK_WATCH=3`, o drain do FIFO ficou minutos dentro de `bind_texture(0)` (`g_rsx_phase=0x100`) sem esperar a GPU (`vk_fence_waiting=0`). O backend Vulkan decodifica na CPU a cadeia mip inteira, até 4096², a cada `BEGIN` com unidade suja, e o GoW reenvia o estado de textura a cada draw. Sem drain, o RSX fica parado num `ACQUIRE` cujo label já foi liberado (`label 0x520 = 0x7F` com `want = 0x7F`), e tudo congela. **Port:** `VK_TEX_OFF=1` por padrão enquanto não houver shaders guest (glslang); `VK_TEX_OFF=0` volta a decodificar. Pendente: um cache que pule o decode quando o estado da unidade não muda.
- **Parada 2, o resync "circling".** Quando 4 drains seguidos esgotam o orçamento de palavras, o walker pula para o `put` (`FIFO resync (circling)`). No GoW, o `put` fica fixo em `0xA80000` e o RSX é controlado por parks de JUMP, então isso jogava fora o resto do frame (`get == put`, RSX ocioso). **Port:** `GCM_FIFO_CIRCLE_PASSES=0` por padrão. Uma solução melhor seria tratar como park um JUMP que volta a um offset já visitado no mesmo drain.
- Com as duas mudanças, chega a cerca de 1300–1600 flips em 36–56 s. A variância entre execuções é grande.
- **Paradas restantes, ainda em investigação:**
  - um self-JUMP de park (`0x38C030`, logo depois de um `SET_REFERENCE 0x86`) com o próximo segmento já escrito e nunca liberado;
  - o walker parado em `0x280520`, num método comum, sem park nem semáforo.
  
  O próximo passo é descobrir quem aplica o patch de um park num frame que funciona (`SPU_DMA_EA` sobre o anel, mais o write watch da PPU) e o que esse agente espera na parada.
- Ferramentas novas: `GOW_CLOCK_WATCH=<s>` (fase do frame clock; mais `[walk]` quando o clock trava ou os flips param), `GCM_SEMALOG=1`, `GOW_MLAA_LOG=1`, `VK_TEX_OFF`.

### M0.22 — a parada restante é um ciclo PPU ↔ RsxSync ↔ RSX (2026-10-17, diagnóstico)
- **Medido e descartado:**
  - publicação de fences: na parada, `ref` bate e a fila está vazia;
  - park liberado e depois sobrescrito por DMA: nenhum `[park-reset]`;
  - update perdido no contador de lote: com `SPU_ATOM_EA=99AA80`, o 6º decremento simplesmente nunca é emitido.
- **Quem libera os parks** (`GCM_PARKLOG=1`):
  - os parks no meio do segmento são liberados pelos jobs EDGE (wids 15/16/17, PM `pc=0xD0F8`, `PUTF` cuja primeira palavra é o park) e pela PPU;
  - o park de início de frame (`…x030`, depois de `SET_REFERENCE`, segmentos de `0x14000` no anel `0x280000–0xA80000`) é liberado pela thread principal (`func_00376A08` ← `0x375D90` ← `func_003E82A8+0x6D8`), que escreve "JUMP para a palavra seguinte".
- **Função de frame `0x3E82A8`:** primeiro espera os lotes (`0x3E799C`), depois monta e libera o segmento. A espera usa `idx = (frame+2) % 3`, depois o contador EDGE `0x99AAC0+idx*4`, e para cada slot `i` marcado com `idx` em `0x99B480+i` espera `0x99AA80+i*4` e `0x99B460+i*4`.
- **Lista do RsxSync (lista 0, `0x23E84180`):** se repete por frame como `{wait R_a; job A (0x9BCxxx); wait R_b; job B (0x9BAxxx/0x9BBxxx); nop}`.
  - O job A do frame F decrementa `0x99AA80+(F mod 8)*4` e `0x99B460+(F mod 8)*4`.
  - O job B do frame F sinaliza à MLAA o lote F+1, com o contador de F+1.
  - Em cada segmento k, os reports `R_a(k)` (`+0x47D4`) e `R_b(k)` (`+0x7B30`) ficam dentro do próprio segmento. Assim o lote k+1 só fecha depois de `R_a(k+1)`, que fica no segmento k+1.
- **Na parada:** o RSX está no park de início do segmento k+1, já parcialmente escrito pela PPU (os parks internos dele já foram liberados), e o RsxSync espera um report desse segmento. A PPU, no início da iteração, espera o contador com count 1 que depende desse mesmo job A. Como o hardware não trava, algum elo da emulação está errado: provavelmente a associação slot/frame (`0x99B480`, `0x99B4BC`) ou a ordem entre a marcação do slot e a espera. O próximo passo é ver quem escreve os bytes de dono `0x99B480+i` e o contador `0x99B4BC`, e quando, comparando com o kick.
- **Correção da leitura acima (M0.23):** a espera de lote termina. O watchdog com pilha mostrou a thread principal parada em outro ponto (ver M0.23).
- Ferramentas novas:
  - `GCM_PARKLOG=1`: `[park]`, `[park-patch]`, `[kick]`, `[park-reset]`, e os kicks recentes no `[walk]`;
  - os ganchos de runtime `g_spu_put_observer` (PUTs SPU) e `g_ppu_jump_store_observer` (stores PPU de palavras JUMP);
  - no `[walk]`, a varredura de `GET_REPORT` em torno do `get` (`GCM_WALK_REPORT=<lo>-<hi>`).

### M0.23 — tarefas SPU de dois estágios: 2 ELFs novos e 4 correções no HLE de tasksets (2026-10-17)
- **A parada de fato.** A pilha guest da thread principal no watchdog (`GOW_CLOCK_WATCH`, `ppu_dump_thread_stack`) mostrou `0x3A5AA4` dormindo (`sys_timer_usleep`) até `*(0x983168) == *(0x98317C)`, chamada de `func_00343608` ← `0x368C64` ← `0x369020` ← `0x36968C` ← função de frame. O bloco `0x983100` é um lote de trabalho com dois estágios:
  - **Estágio 1:** `0x3A5270` cria 5 tarefas com o ELF SPU `0x699000` no taskset `0x20024900`. Cada uma faz o trabalho e incrementa `+0x64`, que começa em 1. A que vê o valor antigo igual ao total (`+0x7C`) é a finalizadora.
  - **Estágio 2:** a finalizadora cria, **pelo lado SPU** (`cellSpursCreateTask` da libspurs SPU, em LS `0xC260`), 5 tarefas com o ELF `0x681880` no taskset `0x20021F80` (`+0x74`/`+0x78`). Essas tarefas incrementam `+0x68`, que a PPU espera chegar ao total.
- **Correções:**
  1. **Dois ELFs SPU novos liftados:** `gowtask1` (`0x699000`, 41796 B, imagem 15) e `gowtask2` (`0x681880`, 56484 B, imagem 16), em `spu/images.json`. Sem eles, o despacho dava MISS e as tarefas nunca rodavam.
  2. **Despacho duplicado de tarefa:**
     - `spurs_taskset_add_task` liga enabled+ready antes do despacho do `CreateTask`, e o bit "running" só era marcado quando a thread começava;
     - uma `task_exited` concorrente (em thread SPU) iniciava a mesma tarefa de novo, e as globais `g_ydkj_real_taskset_ea/taskid` trocavam o id entre threads;
     - com isso, `+0x64`/`+0x68` passavam do total e a igualdade exata nunca acontecia.
     
     Agora `spu_workload_dispatch_task_async(image, size, args, taskset, taskid)` reivindica a tarefa (bit running) sob `ts_lock` antes de despachar, e o `CreateTask` passa `taskset`/`id` explícitos.
  3. **Campos do taskset para a API SPU:** `+0x1890 size` passa a ser `0x1900` (v1) ou `0x2900` (`cellSpursCreateTaskset2`), em vez do tamanho da struct do host, e `+0x1898/+0x189C` (ids de event flag) recebem valores não nulos. A criação SPU recusava o taskset com `0x80410909`.
  4. **Tarefas criadas pelo SPU passam a ser iniciadas:**
     - `cellSpursCreateTaskset` registra o taskset (`spu_taskset_register`);
     - um PUTLLC no cabeçalho de um taskset registrado inicia as tarefas enabled+(ready|pending) que não estão rodando (`spu_taskset_line_committed` → varredura de `task_exited`);
     - o campo ELF que o SPU grava traz flags nos bits baixos (`0x00681884`), que agora são mascarados.
- **Resultado:** `+0x68` chega a 5, a espera da thread principal termina e o jogo segue. As tarefas liftadas são lentas, então os flips ficam espaçados. Nova parada: thread principal em `func_00416A10+0x3C` ← `0x352378` ← `0x354718` ← `0x355BD0` ← `0x366B3C` ← `0x35E8EC` ← função de frame `+0x2C4`.
- Ferramentas novas:
  - `SPU_TS_COMMITLOG=<taskset ea>`: bitsets a cada commit no cabeçalho do taskset, mais as candidatas de cada varredura;
  - a pilha da thread principal (`[GSTACK:stall-main]`) quando os flips param.

### M0.24 — buracos EDGE não preenchidos (em andamento, 2026-10-17)
- As execuções chegam a 1300–2200 flips. A parada da thread principal varia (`0x416338`, escalonador de jobs PPU; `0x375A60`, throttle por `ref` do RSX), mas no lado RSX é sempre a mesma coisa: o walker fica num self-JUMP no meio de um sub-buffer, isto é, um buraco que um job EDGE deveria preencher.
- O RenderJobs (wid 13) escreve blocos de 0x4000 no FIFO com buracos; o EDGE0/1/2 (wid 15/16/17) preenche cada um com comandos (`0x00041740…`).
- Cadeia medida:
  - o job do buraco está na lista do EDGE;
  - a PM do EDGE para numa entrada `(2, slot)` e espera o slot **anterior** do anel de contadores de lote `0x99AA80` (8 slots, índice em `0x99AAA0`), que está com count 1;
  - o HLE do MLAA faz 5 dos 6 decrementos; o último vem de um job do RsxSync (LS `0x4618`);
  - o RsxSync está numa espera de report armado, cujo GET_REPORT não está no caminho percorrido antes do buraco (as cópias no anel são de voltas anteriores).
- **Estrutura medida por frame:**
  - na thread principal: `0x411A30` cria o job Y (lote anterior; espera o 1º report do frame, R1), `0x407470` cria o job X (lote atual; MLAA; espera o 2º report, R2) e `0x3E7104` aloca o próximo lote;
  - no SPU: o MeshProcJobs (wid 14, pc `0x74D4`) escreve `(2, slot)` na lista do EDGE do frame (rodízio EDGE0/1/2), sempre com o slot anterior ao índice;
  - no FIFO de cada frame: cabeçalho → JUMP em `+0x23C` para uma cadeia de sub-buffers (frame+0x10004) → volta em `+0x7FC` → GET_REPORT R1 em `+0x804`.
- **Frame normal:** X → R1 → Y → o EDGE preenche os buracos pós-espera → R2. Os buracos que dependem da espera ficam depois de R1 no caminho do RSX.
- **Frame da parada:** o EDGE preenche os buracos pré-espera (num bloco do RenderJobs em frame+0x8000), mas a cadeia de `+0x23C` (frame+0x10000, o 2º bloco escrito pelo RenderJobs) contém um buraco pós-espera. O RSX chega nele antes de R1, e isso fecha o ciclo.
  - Hipótese: os blocos do RenderJobs caíram uma posição deslocados em relação ao cabeçalho do frame (ordem de alocação no anel PPU × SPU).
- **Correção (mesma data):** essa hipótese de deslocamento foi descartada. O slot de espera é sempre `idx−1` (sem corrida), e as paradas são de **três formas**:
  - **A (pós-wrap):** o cabeçalho do frame vem curto (`+0x220 → +0x348`). A cadeia de segmentos daquele frame contém um segmento inválido (`0x293120 → 0x28C140`, com fim antes do início, no meio do anel), e o fluxo principal sobrescreve o próprio cabeçalho e o sub-bloco do frame antes de o RSX executá-los. Daí o buraco pós-espera antes de R1.
  - **B (PM parada num job pré-espera):** linha `0x00130013`; a cadeia de slots não se aplica aqui.
  - **C:** a thread principal fica em `0x368C64 → 0x343608 → 0x3A5AA4` esperando o lote de tarefas de dois estágios, e o frame nunca é ligado (RSX no park `+0x030`).
- **Como o frame é montado:** cada pedaço do fluxo é um registro `{chave u64, valor u64}` numa tabela em `0x20A80010` (base em `0x81B9F8`), reservado com `count++` atômico em `0x967E60`.
  - Quem escreve: o PPU (`0x368BFC`), o HighestPrio6 (wid 2, pc `0x66DC`, 7 registros por frame) e o RenJobSet (wid 11, PUTLLC `0x107F0` + PUT `0x10894`).
  - Na função de frame, `+0x350` (`0x368C64`) espera o lote, ordena (`0x5BB850`) e converte para `{início, fim}` (`*(0x967E68)`); `+0x5CC` (`0x36971C → 0x369184`) liga os segmentos com JUMPs, pulando as entradas com início 0.
- **Causa da forma A (corrigida):** era o nosso HLE.
  - `_cellGcmSetFlipCommand` → `gcm_flip_into_fifo` tem uma heurística de "dar a volta no anel": se sobram menos de `GCM_FLIP_WRAP_BYTES` (0x40000) até `end` e `get ≥ begin + 0x20000` (comparando endereços), ele grava `JUMP → begin` e faz `current = begin`.
  - No GoW o contexto é um bloco de 64 KB por frame, com callback próprio do título (`0x5D9950 → 0x4158D0`). Logo depois do wrap do anel de 8 MB, o `get` ainda está na volta anterior (endereço maior), então o flip (`0x46DA30 → 0x629024`, no meio do frame) rebobinava `current` para o início do bloco, e o resto do frame sobrescrevia o próprio cabeçalho ainda não executado.
  - Era determinístico: sempre o primeiro frame após o wrap, sempre park em `0x299080`.
  - Correção: só dar a volta quando o contexto usa o callback padrão (o nosso) ou nenhum.
  - Resultado: o jogo passa do frame ~194 e vai até o ~358; nova parada em `0x352378`/`0x416A10`.
- Descartados no caminho: deslocamento dos blocos do RenderJobs, corrida na escolha do slot, atraso de 25 ms por frame (`PPU_DELAY_FN`) e reuso de slot de task (as duplicatas eram as legítimas).
- Algumas paradas se resolvem sozinhas. Em aberto: a dependência atravessa mesmo o buraco, ou a emulação erra a ordem de emissão/armação? Também há um segundo padrão sem explicação: a PM parada numa entrada de job (linha `0x00130013`).
- Ferramentas novas:
  - no relatório `[walk]`: `spu-put`, `ref to park`, `jump`, `report-done` e `armed report`;
  - `GCM_WALK_SCAN`, `GCM_WALK_DUMP`, `GCM_WALK_ARMED` e `GCM_PUTRING`;
  - `SPURS_PM_HANG_LS` e `SPURS_PM_ENDLS_WID`.

### M0.25 — forma C: tarefa de lote rodando duas vezes (2026-10-17)
- `0x3A5AA4` espera `*(0x983168) == *(0x98317C)` (tarefas concluídas == total, com `!=` no laço). Na parada medida (run160), `+0x64 = 7`, `+0x68 = 6` e `+0x7C = 5`: a contagem **passou** do total, então a espera nunca termina.
- Causa (HLE): o `cellSpursCreateTask` do PPU liga os bits enabled+ready do taskset (`spurs_taskset_add_task`) e só depois reivindica a tarefa no despacho.
  - Nessa janela, a saída de outra tarefa do mesmo taskset (`spu_taskset_task_exited`) vê a tarefa pronta e a inicia.
  - A tarefa termina rápido e limpa a marca de "rodando".
  - Aí o despacho do criador não vê duplicata e roda a tarefa de novo.
  - Acontece nos dois estágios: no último lote medido, o PPU criou o 2º estágio (ELF `0x681880`) direto.
- Correção: `spu_taskset_claim()` reivindica a tarefa **antes** de ligar os bits, e o despacho vai como já reivindicado (`spu_workload_dispatch_claimed_task_async`). Se uma instância anterior no mesmo slot ainda estiver rodando, o criador não despacha; a saída dela inicia a nova.
- Resultado (run161): nenhuma duplicata, o lote fecha exato (`+0x68 = +0x7C = 5`) e a execução vai a 2090 flips.
- Nova parada (forma D, a mesma `0x352378 → 0x416A10` do run158): o PPU espera um job "RenJobSet" (registro `*(0x967438)`, grupo `0x966FB0`). A dependência `+0x24` (`0x21600684`) fica em **`0x0013`**, o mesmo valor da linha `0x00130013` da forma B. Provavelmente B e D são a mesma espera vista dos dois lados (WWS RenJobSet).
- Ferramenta: `GCM_WALK_DUMP` aceita desreferência (`ea@off@off…`).

### M0.26 — as paradas variáveis eram motores MFC compartilhados (2026-10-17)
- As formas B, D e o buraco EDGE eram a mesma coisa vista de lados diferentes.
- **A linha `+0x70` das listas WWS** é `{u16 próximo índice, u16 índice}` mais um slot de job em voo por SPU (`FFFF` = livre). Quem escreve é só a própria PM (`pc 0x2540` aloca o job; `pc 0x35E0` conclui). Ninguém "publica" o ticket.
- **O kernel SPURS da porta** roda as PMs de uma instância numa só thread do host. Quando uma execução de PM não retorna, todas as cargas de trabalho param. Medido no run164: as linhas `[spurs-pm] poll` param de vez logo depois de o EDGE1 começar o job `0x12`.
- `SPURS_PM_HANG=4` mostrou a PM presa:
  - no run166, wid 8 (BrBDblBufWrkld) estava no tratador de interrupção (`0x2390`), esperando que a tag de uma lista com stall-and-notify concluísse;
  - no run167, wid 17 (EDGE2) estava no laço principal com `loadJobState = 3`, esperando a tag 1.
  - Nos dois casos não havia lista estacionada (`parked = 0`) e a tag não estava concluída.
- Causa (runtime):
  - `mfc_for(ctx)` tinha **8 slots** de motor MFC indexados pelo ponteiro do contexto.
  - Os contextos de job e de tarefa (na pilha) e os de PM (persistentes, um por wid/lane) nunca devolviam o slot. Logo depois do boot aparecia "more than 8 contexts hold an MFC engine at once; the rest share one", e daí em diante quase todo SPU dividia **um** `tag_completed`.
  - A lista estacionada de um contexto deixava a tag "incompleta" para outro, que girava esperando com o kernel parado.
- Correção:
  - `spu_coh_unregister` (fim de todo contexto transitório) agora devolve o motor;
  - a PM que recria o contexto ("fresh PM context") também devolve o motor;
  - a tabela tem 64 slots, com cache por thread.
- Resultado (run168): nenhuma parada em 560 s, 3020 flips e ainda rodando no fim.
- Ferramentas: `SPU_ATOM_EA` aceita até 4 linhas (`a,b,c,d`) e `SPU_ATOM_PUTONLY=1`; o `[pm-hang]` mostra `tagmask`, `tagstat` e as listas estacionadas.
- **Confirmação:**
  - run169: 30 min sem parada permanente, 4540 flips;
  - run170, com o contador de frames do jogo (`*(0x9710C8)`, via `GOW_HEARTBEAT`): frame **901** aos 570 s. Passou com folga a barreira antiga (~358), a ~1,3–1,6 frames/s e 1 flip por frame.
- **Perfil (run171, `SPURS_PM_PROF=20 SPU_TASK_PROF=20`):**
  - a thread única do kernel SPURS fica **~74% ocupada**: wid 8 (BrBDblBufWrkld) 30% (~165 execuções/s, ~1,8 ms cada) e EDGE0/1/2 ~11% cada; o resto é ≤3%;
  - as tarefas de lote custam 2,7 ms (img15) e 7 ms (img16), sem espera de lock.
  - O gargalo seguinte é o kernel serial: no hardware, essas cargas rodam em SPUs paralelas.
- Padrões novos na porta: `SPURS_PM_HANG=10` e `GOW_HEARTBEAT=<s>` (opcional).
- **Onde vai o tempo do frame (runs 172–173):**
  - **Thread principal** (`GOW_MAIN_PROF`, amostra a cada 2 ms):
    - 53% em `0x3E8388`, o `bl 0x3E799C` da função de frame, esperando o EDGE;
    - 22% em `0x416A4C`, esperando o job RenJobSet;
    - ~13% em outras esperas (`0x3B35B4`, `0x443E9C`).
    - Ou seja, o frame está preso às SPUs.
  - **SPU** (`SPURS_PM_PROF`, agora com a fração interpretada): **90–94% do tempo dos jobs WWS roda no interpretador SPU**. São 90% no wid 8, 94% no EDGE e 98% no RenderJobs.
  - Os módulos de job que a PM carrega em tempo de execução não têm lift. O casador de overlays (`resident_ovl`, `tools/extract_jobmods.py`/`lift_jobmods.py`) foi feito para a PM do LBP (imagem 2), não para a do GoW (imagem 10).
  - **Próxima frente (M3): extrair, fazer o lift e casar os módulos de job WWS do GoW.** Isso rende uma ordem de grandeza; paralelizar o kernel renderia no máximo 2×.

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
