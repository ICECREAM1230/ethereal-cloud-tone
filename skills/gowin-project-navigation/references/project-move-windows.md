# Recover a Gowin project move blocked by Windows file locks

Read this only when a Windows move or rename fails because Gowin or another process has a file open. Ask the user to close the owning process before retrying. Inspect both source and destination and rerun Git status first; a failed operation may have moved only part of the project package.
