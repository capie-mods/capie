"""Typed confirmations before anything leaves the machine.

An action that publishes something (a VirusTotal upload, a public release, a Workshop
item that is not hidden) asks the maintainer to type a word, not just press Enter, so
a habitual "y" does not publish by accident. Without a console every question is
answered "no".
"""

from __future__ import annotations

import sys
from typing import Protocol


class Prompter(Protocol):
    """Asks the maintainer."""

    def confirm(self, question: str, word: str) -> bool:
        """Ask for a typed confirmation.

        Args:
            question: What will happen.
            word: Word that must be typed to agree.

        Returns:
            True only when exactly that word was typed.
        """
        ...

    def ask(self, question: str) -> str:
        """Ask for a free-text answer.

        Args:
            question: Question.

        Returns:
            The answer without surrounding blanks (empty when there is no console).
        """
        ...


class ConsolePrompter:
    """The real :class:`Prompter` (standard input)."""

    def confirm(self, question: str, word: str) -> bool:
        """Ask for a typed confirmation.

        Args:
            question: What will happen.
            word: Word that must be typed to agree.

        Returns:
            True only when exactly that word was typed; False without a console.
        """
        if not sys.stdin.isatty():
            return False
        try:
            answer = input(f"  ASK   {question}\n        Type '{word}' to continue: ")
        except EOFError:
            return False
        return answer.strip() == word

    def ask(self, question: str) -> str:
        """Ask for a free-text answer.

        Args:
            question: Question.

        Returns:
            The answer without surrounding blanks; empty without a console.
        """
        if not sys.stdin.isatty():
            return ""
        try:
            return input(f"  ASK   {question} ").strip()
        except EOFError:
            return ""
