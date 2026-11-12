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
    "version": "v1.0.2",
    "developer": "Creator",
    "description": "AI-powered automatic block & air decoration in the Level Editor (Neon, Modern, Hell, Reality) opened via Right Alt.",
    "settings": {
        "ai-api-url": {
            "type": "string",
            "name": "AI API Endpoint URL",
            "default": "https://api.openai.com/v1/chat/completions"
        },
        "ai-api-key": {
            "type": "string",
            "name": "AI API Key",
            "default": ""
        },
        "ai-model": {
            "type": "string",
            "name": "AI Model Name",
            "default": "gpt-4o-mini"
        },
        "default-block-count": {
            "type": "int",
            "name": "Default Block Count",
            "default": 80,
            "min": 10,
            "max": 500
        },
        "clear-previous-decor": {
            "type": "bool",
            "name": "Tag Generated Decor in Group 999",
            "default": true
        }
    }
}
