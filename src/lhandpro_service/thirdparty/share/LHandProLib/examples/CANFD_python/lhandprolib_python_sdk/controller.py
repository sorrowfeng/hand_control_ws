"""Deployed package entry for CANFD-only examples."""

from .controller_canfd import CANFDController

LHandProLibController = CANFDController

__all__ = ["LHandProLibController", "CANFDController"]
