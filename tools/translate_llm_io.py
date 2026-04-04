#!/usr/bin/env python3
"""
Translate text and token ids for SmolLM/Qwen tokenizers.

This helper reads local tokenizer.json files under tools/tokenizers and only
depends on the third-party `tokenizers` package.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


SCRIPT_DIR = Path(__file__).resolve().parent

DEFAULT_TOKENIZER_FILES = {
    "smol": SCRIPT_DIR / "tokenizers" / "SMOL" / "tokenizer.json",
    "qwen": SCRIPT_DIR / "tokenizers" / "QWEN" / "tokenizer.json",
}

MODEL_ALIASES = {
    "smol": "smol",
    "smollm": "smol",
    "smollm2": "smol",
    "smollm2-135m": "smol",
    "smollm2-135m-instruct": "smol",
    "huggingfacetb/smollm2-135m-instruct": "smol",
    "qwen": "qwen",
    "qwen3.5": "qwen",
    "qwen3.5-0.8b": "qwen",
    "qwen/qwen3.5-0.8b": "qwen",
}


@dataclass(frozen=True)
class TokenizerHandle:
    model_key: str
    source_path: Path
    tokenizer: object


def canonical_model(name: str) -> str:
    key = name.strip().lower()
    try:
        return MODEL_ALIASES[key]
    except KeyError as exc:
        allowed = ", ".join(sorted(DEFAULT_TOKENIZER_FILES))
        raise argparse.ArgumentTypeError(
            f"unsupported model {name!r}; use one of: {allowed}"
        ) from exc


def read_text_arg(text: str | None, text_file: str | None) -> str:
    if (text is None) == (text_file is None):
        raise SystemExit("exactly one of --text or --text-file is required")
    if text is not None:
        return text
    if text_file == "-":
        return sys.stdin.read()
    return Path(text_file).read_text(encoding="utf-8")


def parse_token_blob(raw: str) -> list[int]:
    out: list[int] = []
    for idx, word in enumerate(raw.split()):
        if not word.isdigit():
            raise SystemExit(f"invalid token id at field {idx}: {word!r}")
        value = int(word)
        if value < 0 or value > 0xFFFFFFFF:
            raise SystemExit(f"token id out of uint32 range at field {idx}: {word!r}")
        out.append(value)
    if not out:
        raise SystemExit("no token ids supplied")
    return out


def read_token_arg(tokens: str | None, token_file: str | None) -> list[int]:
    if (tokens is None) == (token_file is None):
        raise SystemExit("exactly one of --tokens or --token-file is required")
    if tokens is not None:
        return parse_token_blob(tokens)
    raw = sys.stdin.read() if token_file == "-" else Path(token_file).read_text(encoding="utf-8")
    return parse_token_blob(raw)


def format_token_line(tokens: Sequence[int]) -> str:
    return " ".join(str(token) for token in tokens)


def load_tokenizer(model_key: str, source_path: str | None) -> TokenizerHandle:
    resolved_path = Path(source_path).expanduser() if source_path else DEFAULT_TOKENIZER_FILES[model_key]
    try:
        from tokenizers import Tokenizer
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "tokenizers is required for this script. "
            "Install it first, for example: pip install tokenizers"
        ) from exc

    if not resolved_path.is_file():
        raise SystemExit(f"tokenizer file not found: {resolved_path}")
    try:
        tokenizer = Tokenizer.from_file(str(resolved_path))
    except Exception as exc:
        raise SystemExit(f"failed to load tokenizer from {resolved_path}: {exc}") from exc

    return TokenizerHandle(
        model_key=model_key,
        source_path=resolved_path,
        tokenizer=tokenizer,
    )


def encode_text(handle: TokenizerHandle, text: str) -> list[int]:
    try:
        encoded = handle.tokenizer.encode(text)
    except Exception as exc:
        raise SystemExit(f"failed to encode text with {handle.source_path}: {exc}") from exc
    return list(encoded.ids)


def decode_tokens(
    handle: TokenizerHandle,
    token_ids: Sequence[int],
    *,
    skip_special_tokens: bool,
) -> str:
    try:
        return handle.tokenizer.decode(list(token_ids), skip_special_tokens=skip_special_tokens)
    except Exception as exc:
        raise SystemExit(f"failed to decode tokens with {handle.source_path}: {exc}") from exc


def command_encode(args: argparse.Namespace) -> int:
    model_key = canonical_model(args.model)
    handle = load_tokenizer(model_key, args.tokenizer_file)
    text = read_text_arg(args.text, args.text_file)
    print(format_token_line(encode_text(handle, text)))
    return 0


def command_decode(args: argparse.Namespace) -> int:
    model_key = canonical_model(args.model)
    handle = load_tokenizer(model_key, args.tokenizer_file)
    tokens = read_token_arg(args.tokens, args.token_file)
    print(decode_tokens(handle, tokens, skip_special_tokens=args.skip_special_tokens))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Translate text and token ids for SmolLM2-135M and Qwen3.5-0.8B.",
        epilog=(
            "Examples:\n"
            "  python tools/translate_llm_io.py encode --model smol --text \"The capital of France is\"\n"
            "  python tools/translate_llm_io.py decode --model qwen --tokens \"11751\""
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    encode_parser = subparsers.add_parser(
        "encode",
        help="encode text into token ids for one model",
    )
    encode_parser.add_argument("--model", required=True, help="model alias: smol or qwen")
    encode_parser.add_argument(
        "--tokenizer-file",
        help="local tokenizer.json override; defaults to tools/tokenizers/<MODEL>/tokenizer.json",
    )
    encode_group = encode_parser.add_mutually_exclusive_group(required=True)
    encode_group.add_argument("--text", help="inline text to encode")
    encode_group.add_argument("--text-file", help="read text from file, or '-' for stdin")
    encode_parser.set_defaults(func=command_encode)

    decode_parser = subparsers.add_parser(
        "decode",
        help="decode token ids back to text for one model",
    )
    decode_parser.add_argument("--model", required=True, help="model alias: smol or qwen")
    decode_parser.add_argument(
        "--tokenizer-file",
        help="local tokenizer.json override; defaults to tools/tokenizers/<MODEL>/tokenizer.json",
    )
    decode_group = decode_parser.add_mutually_exclusive_group(required=True)
    decode_group.add_argument("--tokens", help="whitespace-separated token ids")
    decode_group.add_argument("--token-file", help="read whitespace-separated token ids from file, or '-' for stdin")
    decode_parser.add_argument(
        "--skip-special-tokens",
        action="store_true",
        help="drop tokenizer-defined special tokens during decode",
    )
    decode_parser.set_defaults(func=command_decode)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
