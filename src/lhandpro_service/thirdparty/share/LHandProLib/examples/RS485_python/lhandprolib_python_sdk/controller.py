"""Deployed package entry for RS485-only examples."""

from .controller_rs485 import RS485Controller

LHandProLibController = RS485Controller

__all__ = ["LHandProLibController", "RS485Controller"]
