'''
Test to verify that a binary install worked correctly by running some
demos.
'''

import sys
import os
import time
import platform
from subprocess import call


if __name__ == '__main__' :
    platform = platform.system()
    extension = '.sh'
    if platform == 'Windows':
        extension = '.bat'
    thisPath = os.path.dirname(os.path.abspath(__file__))
    binPath = os.path.abspath(thisPath + "/../bin")
    demoPath = os.path.abspath(thisPath + "/../demo")
    print("Starting " + binPath + "/startServices" + extension)
    call([binPath + "/startServices" + extension])
    time.sleep(5)
    execfile(demoPath + '/prerequisites.py')
    execfile(demoPath + '/detect.py')
    execfile(demoPath + '/training.py')
    execfile(demoPath + '/runset.py')
    execfile(demoPath + '/full_image_corpus.py')
    execfile(demoPath + '/bootstrapping.py')
    time.sleep(5)
    call([binPath + "/stopServices" + extension])

