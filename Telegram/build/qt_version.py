import sys, os

def resolve(arch):
    if sys.platform == 'darwin':
        os.environ['QT'] = '6.2.12'
    elif sys.platform == 'win32':
        if arch == 'arm' or 'qt6' in sys.argv:
            os.environ['QT'] = '6.9.0'
        elif os.environ.get('QT') is None:
            os.environ['QT'] = '5.15.15'
    elif os.environ.get('QT') is None:
        return False
    print('Choosing Qt ' + os.environ.get('QT'))
    return True
