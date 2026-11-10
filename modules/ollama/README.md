# Sparkles Ollama inference module

The Ollama module implements the established STN-LABZ shared module ABI. Sparkles Core routes ordinary conversation to its registered `chat` controller. Identity and mission questions are answered by the registered `sparkles.identity.answer` service when recognized.

## Selected model

**Model:** `qwen3:4b-instruct`

**Endpoint:** `http://127.0.0.1:11434/api/generate`

The selected model is defined by `SPARKLES_OLLAMA_MODEL` in `include/ollama_internal.h`, not an environment variable. **No exports or runtime configuration steps are required.** The model is already reported installed on the operator's Ollama server. Ollama performs model loading on the first generation request; the module does not pull or install models.

## Build and install

Requires the `libcurl` development headers and library.

```sh
cd ~/sparkles
git pull
make -C modules/ollama clean
make -C modules/ollama
make -C modules/ollama test
sudo make -C modules/ollama install INSTALL_USER=stnlabz INSTALL_GROUP=stnlabz
/opt/sparkles/bin/sparkles
```

Installed files:

- `/opt/sparkles/modules/ollama/ollama.so`
- `/opt/sparkles/modules/ollama/module.conf`

The Core hotloader discovers, qualifies, and activates the module, which registers `sparkles.llm.generate` and the `chat` command. A successfully running Ollama server is required for replies, but the actual model inference is a separate runtime test.

## Verification

Source changes have been committed to GitHub. Native compilation, module activation, and model inference remain subject to operator verification.
