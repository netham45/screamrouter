"""Holds exceptions used by ScreamRouter"""

class InUseError(Exception):
    """Called when removal is attempted of something that is in use"""
    def __init__(self, message: str):
        super().__init__(self, message)
