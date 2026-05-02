import os
import yaml
from contextlib import asynccontextmanager
from pathlib import Path

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException

load_dotenv(Path(__file__).parent / ".env")
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from openai import AsyncOpenAI

# ── LLM 配置从环境变量加载 ────────────────────────────────────────────────────

def load_llm_configs() -> dict[str, dict]:
    """从环境变量解析多组 LLM 配置。
    格式：LLM_NAMES=qwen,gpt  + LLM_<NAME>_KEY/URL/MODEL/MAX_TOKENS
    """
    names = [n.strip() for n in os.environ.get("LLM_NAMES", "").split(",") if n.strip()]
    if not names:
        raise RuntimeError("LLM_NAMES 未配置，请在 .env 中设置")

    configs = {}
    for name in names:
        upper = name.upper()
        key   = os.environ.get(f"LLM_{upper}_KEY")
        url   = os.environ.get(f"LLM_{upper}_URL")
        model = os.environ.get(f"LLM_{upper}_MODEL")
        if not all([key, url, model]):
            raise RuntimeError(f"LLM 配置 '{name}' 不完整，需要 KEY / URL / MODEL")
        configs[name.lower()] = {
            "api_key":    key,
            "api_url":    url,
            "model":      model,
            "max_tokens": int(os.environ.get(f"LLM_{upper}_MAX_TOKENS", "150")),
        }
    return configs


def load_buttons(path: str = "config.yaml") -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f).get("buttons", {})


# ── 全局状态 ──────────────────────────────────────────────────────────────────

llm_configs: dict[str, dict]       = {}
llm_clients: dict[str, AsyncOpenAI] = {}
buttons:     dict                   = {}


@asynccontextmanager
async def lifespan(app: FastAPI):
    global llm_configs, llm_clients, buttons
    llm_configs = load_llm_configs()
    llm_clients = {
        name: AsyncOpenAI(api_key=cfg["api_key"], base_url=cfg["api_url"])
        for name, cfg in llm_configs.items()
    }
    buttons = load_buttons()
    print(f"LLM configs: {list(llm_configs.keys())}")
    print(f"Buttons: {len(buttons)} configured")
    yield
    llm_clients.clear()


app = FastAPI(title="ESP32 Button API", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)


# ── 请求 / 响应模型 ───────────────────────────────────────────────────────────

class TriggerRequest(BaseModel):
    button_id: int


class TriggerResponse(BaseModel):
    text: str
    button_id: int


# ── 接口 ──────────────────────────────────────────────────────────────────────

@app.post("/api/trigger", response_model=TriggerResponse)
async def trigger(req: TriggerRequest):
    btn_cfg = buttons.get(req.button_id)
    if btn_cfg is None:
        raise HTTPException(status_code=404, detail=f"Button {req.button_id} not configured")

    llm_name = btn_cfg["llm_config"].lower()
    cfg      = llm_configs.get(llm_name)
    client   = llm_clients.get(llm_name)
    if client is None:
        raise HTTPException(status_code=500, detail=f"LLM config '{llm_name}' not found")

    try:
        response = await client.chat.completions.create(
            model=cfg["model"],
            max_tokens=cfg["max_tokens"],
            messages=[
                {"role": "system", "content": btn_cfg["system_prompt"]},
                {"role": "user",   "content": "请现在给出内容。"},
            ],
        )
        text = response.choices[0].message.content.strip()
    except Exception as e:
        print(f"LLM error (button={req.button_id}, llm={llm_name}): {e}")
        text = "服务繁忙，请稍后再试"

    return TriggerResponse(text=text, button_id=req.button_id)


@app.get("/health")
async def health():
    return {
        "status":  "ok",
        "llms":    list(llm_configs.keys()),
        "buttons": {k: {"name": v.get("name", f"K{k}")} for k, v in buttons.items()},
    }
