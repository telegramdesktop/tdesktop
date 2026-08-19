import sys, os

def resolve(arch):
    if sys.platform == 'darwin':
        os.environ['QT'] = '6.11.1'
    elif sys.platform == 'win32':
        if arch == 'arm' or 'qt6' in sys.argv:
            print('Choosing Qt 6.')
            os.environ['QT'] = '6.11.1'
        else:
            print('Choosing Qt 5.')
            os.environ['QT'] = '5.15.19'
    return True
