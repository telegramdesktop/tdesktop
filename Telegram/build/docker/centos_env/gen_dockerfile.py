#!/usr/bin/env python3
from os import environ
from os.path import dirname
from jinja2 import Environment, FileSystemLoader

def checkEnv(envName, defaultValue):
    return bool(len(environ[envName])) if envName in environ else defaultValue

def main():
    print(Environment(loader=FileSystemLoader(dirname(__file__))).get_template("Dockerfile").render(
        DEBUG=checkEnv("DEBUG", True),
        LTO=checkEnv("LTO", True),
    ))

if __name__ == '__main__':
    main()
