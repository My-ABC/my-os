from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class IntegrationFailure(RuntimeError):
    pass
