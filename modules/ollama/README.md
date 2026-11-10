# Sparkles Ollama inference module

This module implements the existing STN-LABZ shared module ABI. The Core router forwards ordinary conversation to the module's registered `chat` controller, while the module calls a separately running local Ollama service for inference. The Identity service remains authoritative for recognized identity, mission and General Order questions.

## Model selection

There is **no hardcoded production model**. The operator chooses one of the models installed in Ollama through `SPARKLES_OLLAMA_MODEL`.

```sh
ollama list
export SPARKLES_OLLAMA_MODEL='<the-installed-model-name>'
/opt/sparkles/bin/sparkles
```

The environment variable must be present in Sparkles' **runtime environment**, before the agent starts. If it is absent, the module refuses to activate and reports the missing setting rather than silently selecting an unwanted model. The Core can still run with other qualified modules.

Ollama must be running and the selected model must be installed and accessible locally. The Ollama API endpoint remains `http://127.0.0.1:11434/api/generate`; Ollama, not the module, loads the requested model. The module does not pull or install models.

## Build and install

Requires the `libcurl` development headers and library.

```sh
cd ~/sparkles/modules/ollama
make clean && make && make test
sudo make install INSTALL_USER=stnlabz INSTALL_GROUP=stnlabz
```

Installed files:

- `/opt/sparkles/modules/ollama/ollama.so`
- `/opt/sparkles/modules/ollama/module.conf`

The module is activated by Sparkles Core hotloading after verification and qualification. It registers `sparkles.llm.generate` and the `chat` command through the existing host API. Identity questions are delegated to `sparkles.identity.answer`; unrecognized questions go to the selected Ollama model.

## Validation limits

Source and build configuration are committed, but a native WSL2 build, module installation, runtime hotload, and actual Ollama inference must be verified by the operator. The module test suite checks request construction and response extraction without requiring a live Ollama server.
