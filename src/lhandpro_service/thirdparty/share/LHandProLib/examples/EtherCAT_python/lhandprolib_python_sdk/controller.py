"""Deployed package entry for EtherCAT-only examples."""

from .controller_ecat import EtherCATController

LHandProLibController = EtherCATController

__all__ = ["LHandProLibController", "EtherCATController"]
