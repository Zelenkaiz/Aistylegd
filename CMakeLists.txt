

Вы достигли лимита на использование
Задачи снова станут доступны 12 нояб. в 06:57. Чтобы увеличить лимит, перейдите на другой тариф.


Значок JSON-файла
mod
{
  "geode": "5.6.0",
  "gd": {
    "win": "2.2081",
    "android": "2.2081",
    "mac": "2.2081",
    "ios": "2.2081"
  },
  "id": "dev.aidecorator",
  "name": "AI Style Decorator",
  "version": "v1.0.1",
  "developer": "Creator",
  "description": "AI-powered automatic block & air decoration in the Level Editor (Neon, Modern, Hell, Reality) opened via Right Alt.",
  "settings": {
    "ai-api-url": {
      "type": "string",
      "name": "AI API Endpoint URL",
      "description": "OpenAI-compatible chat completions endpoint (e.g., https://api.openai.com/v1/chat/completions or local Ollama http://localhost:11434/v1/chat/completions). Leave empty or without key to use the built-in offline AI procedural engine.",
      "default": "https://api.openai.com/v1/chat/completions"
    },
    "ai-api-key": {
      "type": "string",
      "name": "AI API Key",
      "description": "Your API key for cloud LLM generation. If left empty, the mod uses its built-in smart procedural generator with prompt parsing.",
      "default": ""
    },
    "ai-model": {
      "type": "string",
      "name": "AI Model Name",
      "description": "Model name to request (e.g., gpt-4o-mini, gemini-2.5-flash, llama3).",
      "default": "gpt-4o-mini"
    },
    "default-block-count": {
      "type": "int",
      "name": "Default Block Count",
      "description": "Default target number of decoration objects to generate inside the selected area.",
      "default": 80,
      "min": 10,
      "max": 500
    },
    "clear-previous-decor": {
      "type": "bool",
      "name": "Tag Generated Decor in Group 999",
      "description": "Automatically assigns group ID 999 to all generated decoration objects so you can easily select or delete them.",
      "default": true
    }
  }
}
