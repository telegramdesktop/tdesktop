#!/usr/bin/env python3
from os import environ
from os.path import dirname
from jinja2 import Environment, FileSystemLoader

def checkEnv(envName, defaultValue):
    if isinstance(defaultValue, bool):
        return bool(len(environ[envName])) if envName in environ else defaultValue
    return environ[envName] if envName in environ else defaultValue

def main():
    print(Environment(loader=FileSystemLoader(dirname(__file__))).get_template("Dockerfile").render(
        DEBUG=checkEnv("DEBUG", True),
        LTO=checkEnv("LTO", True),
        JOBS=checkEnv("JOBS", ""),
    ))

if __name__ == '__main__':
    main()
