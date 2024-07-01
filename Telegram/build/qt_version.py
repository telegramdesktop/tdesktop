import sys, os

def resolve(arch):
    if sys.platform == 'darwin':
        os.environ['QT'] = '6.2.8'
    elif sys.platform == 'win32':
        if arch == 'arm' or 'qt6' in sys.argv:
            print('Choosing Qt 6.')
            os.environ['QT'] = '6.7.2'
        elif os.environ.get('QT') is None:
            print('Choosing Qt 5.')
            os.environ['QT'] = '5.15.13'
    elif os.environ.get('QT') is None:
        return False
    print('Choosing Qt ' + os.environ.get('QT'))
    return True
