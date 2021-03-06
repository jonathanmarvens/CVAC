'''
Easy!  mini tutorial
Invoke a remote service, send files, receive files, receive messages
matz 6/17/2013
'''

import easy

#
# Example 1: Test on a remote machine.
# specify the host name of the service
#
host = "-h vision.nps.edu"

#
# create a simple RunSet with just one unlabeled image
#
rs1 = easy.createRunSet( "testImg/italia.jpg" )

#
# Make sure all files in the RunSet are available on the remote site;
# it is the client's responsibility to upload them if not.
# The putResult contains information about which files were actually transferred.
#
fileserver = easy.getFileServer( "FileService:default -p 10110 " + host )
putResult = easy.putAllFiles( fileserver, rs1 )
modelfile = "detectors/haarcascade_frontalface_alt.xml"
if not fileserver.exists( easy.getCvacPath(modelfile) ):
    easy.putFile( fileserver, easy.getCvacPath(modelfile) )

#
# detect remotely: note the host specification
#
print("------- Remote detection, local result display: -------")
detector = easy.getDetector( "OpenCVCascadeDetector:default -p 10102 "+host )
results = easy.detect( detector, modelfile, rs1 )
easy.printResults( results )

#
# Example 2:
# Train on a remote machine, obtain the model file, and test locally.
# Assume the files are on the remote machine, or transfer with putAllFiles.
#
trainer = easy.getTrainer( "BOW_Trainer:default -p 10103 "+ host) # remote
trainset = easy.createRunSet( "trainImg" );
trainedModel = easy.train( trainer, trainset )
easy.getFile( fileserver, trainedModel )  # downloads the model from remote
print("{0}".format(trainedModel))
detector = easy.getDetector( "BOW_Detector:default -p 10104" ) # local service
testset = easy.createRunSet("testImg","UNPURPOSED"  )
results = easy.detect( detector, trainedModel, testset )
