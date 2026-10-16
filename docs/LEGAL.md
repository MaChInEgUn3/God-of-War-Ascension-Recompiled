# Legal and distribution policy

This is not legal advice.

* The repository ships **only** original code, patches against MIT-licensed upstream, scripts and documentation.
* **Never commit**: game dumps or assets (PSARC, WAD, textures, audio, video), `EBOOT.BIN` or the decrypted ELF, the
  PARAM.SFO of a retail disc, console firmware or anything decrypted from it (`libsre.prx`, PUP), lifted C/C++ of the
  game or of firmware (it is a translation, i.e. a derivative work), logs that embed game memory, save data, keys or
  credentials.
* Static recompilation produces a derivative of the binary. An executable that embeds lifted game or firmware code
  must not be redistributed. Users build locally from their own legally acquired copy.
* Firmware is strictly bring-your-own: decryption is done by the user with their own tools. This repository contains no
  keys and no code that circumvents protection.
* `.gitignore` and the CI check `no-forbidden-files` are guard rails, not a substitute for judgement: review
  `git status` before every commit.
