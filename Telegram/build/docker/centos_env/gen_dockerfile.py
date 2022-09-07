#!/usr/bin/env python3
from os import environ
from os.path import dirname
from jinja2 import Environment, FileSystemLoader

def main():
    print(Environment(loader=FileSystemLoader(dirname(__file__))).get_template("Dockerfile").render(
        DEBUG=bool(len(environ["DEBUG"])) if "DEBUG" in environ else True
    ))

if __name__ == '__main__':
    main()
