/*
 
 Right to copy.
 Written by Radu Pascal (aka haiduc32)
 
 */
#include "OneWirelessGamingReceiver.h"
#include "WirelessDevice.h"
#include "devices.h"

//#define PROTOCOL_DEBUG

OSDefineMetaClassAndStructors(OneWirelessGamingReceiver, IOService)

// Holds data for asynchronous reads
typedef struct WGRREAD
{
    IOUSBPipe *pipe;
    IOBufferMemoryDescriptor *buffer;
} WGRREAD;

// Get maximum packet size for a pipe
static UInt32 GetMaxPacketSize(IOUSBPipe *pipe)
{
    const IOUSBEndpointDescriptor *ed = pipe->GetEndpointDescriptor();
    
    if (ed == NULL) return 0;
    else return ed->wMaxPacketSize;
}

static char char2int(char input)
{
    if(input >= '0' && input <= '9')
        return input - '0';
    if(input >= 'A' && input <= 'F')
        return input - 'A' + 10;
    if(input >= 'a' && input <= 'f')
        return input - 'a' + 10;
    return 0;
}


static void HexToBytes(const char input[], char* output, unsigned long hexLength)
{
    for (int i = 0; i < hexLength / 2; i++)
    {
        *(output + i) = char2int(input[i*2])*16 + char2int(input[i*2+1]);
    }
}

static void HexToBytes(const char input[], unsigned char* output, unsigned long hexLength)
{
    for (int i = 0; i < hexLength / 2; i++)
    {
        *(output + i) = char2int(input[i*2])*16 + char2int(input[i*2+1]);
    }
}

// Start device
bool OneWirelessGamingReceiver::start(IOService *provider)
{
    
    IOLog("WirelessGamingReceiver::start\n");
    
    const IOUSBConfigurationDescriptor *cd;
    IOUSBFindInterfaceRequest interfaceRequest;
    IOUSBFindEndpointRequest pipeRequest;
    IOUSBInterface *interface;
    int iConnection, iOther, i;
    IOUSBPipe *pipe = NULL;
    
    char outBuff[4];
    char outHex[] = "098840c0";
    HexToBytes(outHex, outBuff, 8);
    
    
    IOUSBDevRequest	request;
    int err;
    
    if (!IOService::start(provider))
    {
        IOLog("start - superclass failed\n");
        return false;
    }
    
    device = OSDynamicCast(IOUSBDevice, provider);
    
    if (device == NULL)
    {
        IOLog("start - invalid provider\n");
        goto fail;
    }
    
    // Check for configurations
    if (device->GetNumConfigurations() < 1)
    {
        device = NULL;
        IOLog("start - device has no configurations!\n");
        goto fail;
    }
    
    // Set configuration
    cd = device->GetFullConfigurationDescriptor(0);
    if (cd == NULL)
    {
        device = NULL;
        IOLog("start - couldn't get configuration descriptor\n");
        goto fail;
    }
    
    if (!device->open(this))
    {
        device = NULL;
        IOLog("start - failed to open device\n");
        goto fail;
    }
    if (device->SetConfiguration(this, cd->bConfigurationValue, true) != kIOReturnSuccess)
    {
        IOLog("start - unable to set configuration\n");
        goto fail;
    }
    
    pipeRequest.interval = 0;
    pipeRequest.maxPacketSize = 0;
    pipeRequest.type = kUSBAnyType;
    interfaceRequest.bInterfaceClass = kIOUSBFindInterfaceDontCare;
    interfaceRequest.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    interfaceRequest.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    interfaceRequest.bAlternateSetting = 0;
    interface = NULL;
    iConnection = 0;
    iOther = 0;
    
    while ((interface = device->FindNextInterface(interface, &interfaceRequest)) != NULL)
    {
        
        IOLog("Entering while\n");
        switch (interface->GetInterfaceProtocol())
        {
            case 255:
                IOLog("Entering case 255\n");
                if (!interface->open(this))
                {
                    IOLog("start: Failed to open control interface\n");
                    goto fail;
                }
                
                IOLog("In endpoints:\n");
                pipe = NULL;
                pipeRequest.direction = kUSBIn;//kUSBAnyDirn;
                
                while ((pipe = interface->FindNextPipe(pipe, &pipeRequest)))
                {
                    //this always seems to be the in pipe
                    if (pipe->GetEndpointNumber() == 5)
                    {
                        inDevicePipe = pipe;
                    }
                    
                    IOLog("Endpoint: %d addr: %d type: %d\n", pipe->GetEndpointNumber(),
                          pipe->GetAddress(),
                          pipe->GetType());
                }
                
                IOLog("Out endpoints:\n");
                pipe = NULL;
                pipeRequest.direction = kUSBOut;//kUSBAnyDirn;
                
                while ((pipe = interface->FindNextPipe(pipe, &pipeRequest)))
                {
                    //this always seems to be the out pipe
                    if (pipe->GetEndpointNumber() == 4)
                    {
                        outDevicePipe = pipe;
                    }
                    
                    IOLog("Endpoint: %d addr: %d type: %d\n", pipe->GetEndpointNumber(),
                          pipe->GetAddress(),
                          pipe->GetType());
                }
                
                break;
            default:
                IOLog("start: Ignoring interface (protocol %d)\n", interface->GetInterfaceProtocol());
                
                IOLog("start: Ignoring interface (interface string index %d)\n", interface->GetInterfaceStringIndex());
                break;
        }
    }
    
    // Send the SET_CONFIG request on the bus
    
    bzero( &request, sizeof(IOUSBDevRequest));
    
    request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
    request.bRequest = kUSBRqSetConfig;
    request.wValue = cd->bConfigurationValue;
    request.wIndex = 0;
    request.wLength = 0;
    request.pData = 0;
    err = device->DeviceRequest(&request, 5000, 0);
    IOLog("SET_CONFIG status: %d\n", err);
    
    
    
    // send CONTROL request on the bus
    
    if (!controlIn ( 7, 0, 0x0024, "09883081")) goto fail;
    if (!controlOut( 6, 0, 0x0024, "098840c0")) goto fail;
    if (!controlIn ( 7, 0, 0x0024, "14884080")) goto fail;
    if (!controlIn ( 7, 0, 0x002c, "00000000")) goto fail;
    if (!controlIn ( 7, 0, 0x0024, "14884080")) goto fail;
    if (!controlOut( 6, 0, 0x0024, "148840c0")) goto fail;
    if (!controlIn ( 7, 0, 0x0024, "14884080")) goto fail;
    if (!controlIn ( 7, 0, 0x0030, "00000000")) goto fail;
    if (!controlIn ( 7, 0, 0x0000, "44003276")) goto fail;
    if (!controlIn ( 7, 0, 0x1000, "00306276")) goto fail;
    if (!controlOut( 6, 0, 0x0080, "09020000")) goto fail;
    if (!controlIn ( 7, 0, 0x014c, "00141f00")) goto fail;
    if (!controlOut( 6, 0, 0x0080, "0f020000")) goto fail;
    if (!controlIn ( 7, 0, 0x014c, "00141f00")) goto fail;
    if (!controlIn (71, 0, 0x0230, "00000000")) goto fail;
    if (!controlOut(70, 0, 0x9018, "1838e400")) goto fail;
    if (!controlIn (71, 0, 0x9018, "1838e400")) goto fail;
    if (!controlOut( 6, 0, 0x0800, "01000000")) goto fail;
    if (!controlOut( 6, 0, 0x09a0, "30024000")) goto fail;
    if (!controlOut( 6, 0, 0x09a4, "01000000")) goto fail;
    if (!controlOut( 6, 0, 0x09a8, "01000000")) goto fail;
    if (!controlOut( 6, 0, 0x09c4, "44000000")) goto fail;
    if (!controlOut( 6, 0, 0x0a6c, "03000000")) goto fail;
    
    //write the first chunck of firmware
    //identifier for the chunck of firmware
    if (!controlOut(70, 0, 0x0230, "40000800")) goto fail;
    //ready to receive firmware?
    if (!controlIn (71, 0, 0x0234, "00000000")) goto fail;
    //here comes one big chunk of firmware!
    if (!controlOut(70, 0, 0x0234, "00000038")) goto fail;
    
    if (!QueueWrite(outDevicePipe, GetFirmware(1)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 1\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //write the second chunck of firmware
    //check that firmware has been received? //this packet type should be checked untill the proper code is received
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    
    //identifier for the chunck of firmware
    if (!controlOut(70, 0, 0x0230, "40380800")) goto fail;
    //check again that firmware has been received/ready to receive new firmware chunk
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    //here comes one big chunk of firmware!
    if (!controlOut(70, 0, 0x0234, "00000038")) goto fail;
    
    if (!QueueWrite(outDevicePipe, GetFirmware(2)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 2\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //write third chunck of firmware
    //check that firmware has been received?
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    //identifier for the chunck of firmware
    if (!controlOut(70, 0, 0x0230, "40700800")) goto fail;
    //check again that firmware has been received/ready to receive new firmware chunk
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    //here comes one big chunk of firmware!
    if (!controlOut(70, 0, 0x0234, "00000038")) goto fail;
    
    if (!QueueWrite(outDevicePipe, GetFirmware(3)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 3\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //write fourth chunck of firmware
    //check that firmware has been received?
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    //identifier for the chunck of firmware
    if (!controlOut(70, 0, 0x0230, "40a80800")) goto fail;
    //check again that firmware has been received/ready to receive new firmware chunk
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    //here comes one big chunk of firmware!
    if (!controlOut(70, 0, 0x0234, "00000038")) goto fail;
    
    if (!QueueWrite(outDevicePipe, GetFirmware(4)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 4\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //write fith chunck of firmware
    //check that firmware has been received?
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    //identifier for the chunck of firmware
    if (!controlOut(70, 0, 0x0230, "40e00800")) goto fail;
    //check again that firmware has been received/ready to receive new firmware chunk
    if (!controlIn (71, 0, 0x0234, "000000f8")) goto fail;
    // why did it change? does it have another function?
    if (!controlOut(70, 0, 0x0234, "0000780d")) goto fail;
    
    if (!QueueWrite(outDevicePipe, GetFirmware(5)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 5\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //write sixth chunck of firmware
    //check that firmware has been received?
    if (!controlIn (71, 0, 0x0234, "000078cd")) goto fail;
    //identifier for the chunck of firmware
    if (!controlOut(70, 0, 0x0230, "00081100")) goto fail;
    //check again that firmware has been received/ready to receive new firmware chunk
    if (!controlIn (71, 0, 0x0234, "000078cd")) goto fail;
    // why did it change? does it have another function?
    if (!controlOut(70, 0, 0x0234, "0000a023")) goto fail;
    
    if (!QueueWrite(outDevicePipe, GetFirmware(6)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 6\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //write seventh chunck of firmware
    //check that firmware has been received?
    if (!controlIn (71, 0, 0x0234, "0000a0e3")) goto fail;
    //identifier for the chunck of firmware
    if (!controlOut(70, 0, 0x0230, "00000800")) goto fail;
    //check again that firmware has been received/ready to receive new firmware chunk
    if (!controlIn (71, 0, 0x0234, "0000a0e3")) goto fail;
    // why did it change? does it have another function?
    if (!controlOut(70, 0, 0x0234, "00004000")) goto fail;
    
    if (!QueueWrite(outDevicePipe, GetFirmware(7)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 7\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //not firmware per se (IMHO), but still part of initialization
    //write eigth chunck of firmware
    //check that firmware has been received?
    if (!controlIn (71, 0, 0x0234, "000040c0")) goto fail;
    // finish writing firmware?
    if (!controlOut(70, 0, 0x0230, "00000000")) goto fail;
    
    //?? clear something?
    if (!controlOut(1, 0x12, 0x00, 0)) goto fail;
    
    //??
    if (!controlIn (71, 0, 0x0230, "00000000")) goto fail;
    //?? (tripple retry)
    IOSleep(100); //<- might have solved the need for tripple retry..
    if (!controlIn (71, 0, 0x0230, "01000000"))
    {
        IOSleep(100);
        if (!controlIn (71, 0, 0x0230, "01000000"))
        {
            IOSleep(100);
            if (!controlIn (71, 0, 0x0230, "01000000")) goto fail;
        }
    }
    
    if (!QueueWrite(outDevicePipe, GetFirmware(8)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 8\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //write nineth chunck of firmware
    if (!QueueWrite(outDevicePipe, GetFirmware(9)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 9\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    
    //write tenth chunck of firmware
    if (!QueueWrite(outDevicePipe, GetFirmware(10)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 10\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //need to read from endpoint 5.. 3 packets..
    
    QueueRead(inDevicePipe);
    QueueRead(inDevicePipe);
    QueueRead(inDevicePipe);
    
    if (!controlIn (7, 0, 0x0038, "98023b01")) goto fail;
    if (!controlOut(6, 0, 0x0038, "98023b01")) goto fail;
    
    //now there are a shitload of packets that I have no idea what are used for..
    //1.pcap - 212
    if (!controlOut(6, 0, 0x1004, "03000000")) goto fail;
    if (!controlOut(6, 0, 0x0238, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x1004, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x1204, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x0070, "6464006b")) goto fail;
    if (!controlOut(6, 0, 0x0208, "70000000")) goto fail;
    if (!controlOut(6, 0, 0x0214, "73220000")) goto fail;
    if (!controlOut(6, 0, 0x0218, "44230000")) goto fail;
    if (!controlOut(6, 0, 0x021c, "aa340000")) goto fail;
    if (!controlOut(6, 0, 0x0230, "00120400")) goto fail;
    if (!controlOut(6, 0, 0x0250, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x0400, "000c0800")) goto fail;
    if (!controlOut(6, 0, 0x0408, "1f1fbf1f")) goto fail;
    if (!controlOut(6, 0, 0x0800, "01000000")) goto fail;
    if (!controlOut(6, 0, 0x1004, "0c000000")) goto fail;
    if (!controlOut(6, 0, 0x1404, "13000000")) goto fail;
    if (!controlOut(6, 0, 0x1018, "ff3f3e00")) goto fail;
    if (!controlOut(6, 0, 0x1030, "5598fcff")) goto fail;
    if (!controlOut(6, 0, 0x1034, "ff000000")) goto fail;
    if (!controlOut(6, 0, 0x1104, "09010000")) goto fail;
    if (!controlOut(6, 0, 0x1204, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x1300, "20430600")) goto fail;
    if (!controlOut(6, 0, 0x1304, "00470a00")) goto fail;
    if (!controlOut(6, 0, 0x1308, "38320400")) goto fail;
    if (!controlOut(6, 0, 0x130c, "2f210300")) goto fail;
    if (!controlOut(6, 0, 0x1328, "0f0f1500")) goto fail;
    if (!controlOut(6, 0, 0x1330, "01101000")) goto fail;
    if (!controlOut(6, 0, 0x1334, "00000100")) goto fail;
    if (!controlOut(6, 0, 0x1340, "3f580000")) goto fail;
    if (!controlOut(6, 0, 0x1344, "202b0900")) goto fail;
    if (!controlOut(6, 0, 0x1348, "900f0a00")) goto fail;
    if (!controlOut(6, 0, 0x134c, "0f1fd047")) goto fail;
    if (!controlOut(6, 0, 0x1364, "0300f403")) goto fail;
    if (!controlOut(6, 0, 0x1368, "0300f403")) goto fail;
    if (!controlOut(6, 0, 0x136c, "04207401")) goto fail;
    if (!controlOut(6, 0, 0x1374, "04207401")) goto fail;
    if (!controlOut(6, 0, 0x1378, "8420f403")) goto fail;
    if (!controlOut(6, 0, 0x1380, "dc002c00")) goto fail;
//next: 326

    
    return true;
    
fail:
    IOLog("fail\n");
    ReleaseAll();
    return false;
}

void OneWirelessGamingReceiver::waitWriteCompleted()
{
    int sleptMillis;
    
    sleptMillis = 0;
    //wait till the firmware chunck has been transmitted
    while (!deviceWriteCompleted)
    {
        //sleep for one millisecond
        IOLog("Sleeping 1..\n");
        IOSleep(1);
        sleptMillis++;
        if (sleptMillis >= 10) break;
    }
}

void OneWirelessGamingReceiver::waitReadCompleted()
{
    //TODO: do we really need it?
}


bool OneWirelessGamingReceiver::controlIn(UInt8 bRequest, UInt16 wValue, UInt16 wIndex, const char checkControlHex[])
{
    unsigned char inBuff[4];
    unsigned char expectedBuff[4];
    IOUSBDevRequest request;
    int err;
    
    HexToBytes(checkControlHex, expectedBuff, 8);
    
    bzero( &request, sizeof(IOUSBDevRequest));
    
    request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBVendor, kUSBDevice);
    request.bRequest = bRequest;
    request.wValue = wValue;
    request.wIndex = wIndex;
    request.wLength = 4;
    request.pData = inBuff;
    err = device->DeviceRequest(&request, 5000, 0);
    IOLog("CONTROL In status: %d : %02x%02x%02x%02x\n", err, inBuff[0], inBuff[1], inBuff[2], inBuff[3]);
    if (err != 0)
    {
        IOLog("There was an error: %d\n", err);
        return false;
    }
    
    for (int i = 0; i < 4; i++)
    {
        if (inBuff[i] != expectedBuff[i])
        {
            IOLog("Expected control value did not match!\n");
            return false;
        }
    }
    //TODO: compare inBuff with checkControl and print an error message plus return false
    return true;
}

bool OneWirelessGamingReceiver::controlOut(UInt8 bRequest, UInt16 wValue, UInt16 wIndex, const char controlHex[])
{
    unsigned char outBuff[4];
    IOUSBDevRequest request;
    int err;
    
    if (controlHex != 0) HexToBytes(controlHex, outBuff, 8);
    
    bzero( &request, sizeof(IOUSBDevRequest));
    
    request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    request.bRequest = bRequest;
    request.wValue = wValue;
    request.wIndex = wIndex;
    if (controlHex != 0)
    {
        request.wLength = 4;
        request.pData = outBuff;
    }
    err = device->DeviceRequest(&request, 5000, 0);
    if (controlHex !=0)
    {
        IOLog("CONTROL Out status: %d : %02x%02x%02x%02x\n", err, outBuff[0], outBuff[1], outBuff[2], outBuff[3]);
    }
    else
    {
        IOLog("CONTROL Out status: %d\n", err);
    }
    //TODO: compare inBuff with checkControl and print an error message plus return false
    return true;
}

// Stop the device
void OneWirelessGamingReceiver::stop(IOService *provider)
{
    IOLog("stop\n");
    ReleaseAll();
    IOService::stop(provider);
}

// Handle termination
bool OneWirelessGamingReceiver::didTerminate(IOService *provider, IOOptionBits options, bool *defer)
{
    IOLog("didTErminate\n");
    // release all objects used and close the device
    ReleaseAll();
    return IOService::didTerminate(provider, options, defer);
}


// Handle message from provider
IOReturn OneWirelessGamingReceiver::message(UInt32 type,IOService *provider,void *argument)
{
    IOLog("Message type: %d\n", type);
#if 0
    switch(type) {
        case kIOMessageServiceIsTerminated:
        case kIOMessageServiceIsRequestingClose:
            
            if(device->isOpen(this)) ReleaseAll();
            return kIOReturnSuccess;
        default:
            return IOService::message(type,provider,argument);
    }
#else
    return IOService::message(type,provider,argument);
#endif
}

// Queue a read on a controller
bool OneWirelessGamingReceiver::QueueRead(IOUSBPipe *pipe)
{
    IOLog("QueueRead on pipe %d\n", pipe);
    
    IOUSBCompletion complete;
    IOReturn err;
    WGRREAD *data = (WGRREAD*)IOMalloc(sizeof(WGRREAD));
    
    if (data == NULL)
        return false;
    
    data->pipe = pipe;
    data->buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, GetMaxPacketSize(pipe));
    if (data->buffer == NULL)
    {
        IOFree(data, sizeof(WGRREAD));
        return false;
    }
    
    complete.target = this;
    complete.action = _ReadComplete;
    complete.parameter = data;
    
    err = pipe->Read(data->buffer, 0, 0, data->buffer->getLength(), &complete);
    if (err == kIOReturnSuccess)
        return true;
    
    data->buffer->release();
    IOFree(data, sizeof(WGRREAD));
    
    // IOLog("read - failed to start (0x%.8x)\n", err);
    return false;
}

// Handle a completed read on a controller
void OneWirelessGamingReceiver::ReadComplete(void *parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    IOLog("ReadComplete\n");
    WGRREAD *data = (WGRREAD*)parameter;
    bool reread = true;
    
    switch (status)
    {
        case kIOReturnOverrun:
            IOLog("read - kIOReturnOverrun, clearing stall\n");
            //connections[data->index].controllerIn->ClearStall();
            // fall through
            break;
        case kIOReturnSuccess:
            //TODO: something..
            ProcessMessage((unsigned char*)data->buffer->getBytesNoCopy(), (int)data->buffer->getLength() - bufferSizeRemaining);
            break;
            
        case kIOReturnNotResponding:
            IOLog("read - kIOReturnNotResponding\n");
            // fall through
            break;
        default:
            IOLog("Unknown status: %d\n", status);
            reread = false;
            break;
    }
    
    //int newIndex = data->index;
    data->buffer->release();
    IOFree(data, sizeof(WGRREAD));
    
    //if (reread)
    //    QueueRead(newIndex);
}

bool OneWirelessGamingReceiver::QueueWrite(IOUSBPipe *pipe, IOBufferMemoryDescriptor *outBuffer)
{
    IOLog("QueueWrite\n");
    IOUSBCompletion complete;
    IOReturn err;
    
    deviceWriteCompleted = false;
    
    complete.target = this;
    complete.action = _WriteComplete;
    complete.parameter = outBuffer;
    
    //TODO: check that getCapacity() returns the length of the buffer!!!
    err = outDevicePipe->Write(outBuffer, 0, 0, outBuffer->getCapacity(), &complete);
    if (err == kIOReturnSuccess)
        return true;
    else
    {
        IOLog("send - failed to start (0x%.8x)\n",err);
        return false;
    }
    
}

// Handle a completed write on a controller
void OneWirelessGamingReceiver::WriteComplete(void *parameter,IOReturn status,UInt32 bufferSizeRemaining)
{
    IOLog("WriteComplete\n");
    IOMemoryDescriptor *memory=(IOMemoryDescriptor*)parameter;
    if(status!=kIOReturnSuccess) {
        IOLog("write - Error writing: 0x%.8x\n",status);
    }
    memory->release();
    
    deviceWriteCompleted = true;
}

// Release any allocated objects
void OneWirelessGamingReceiver::ReleaseAll(void)
{
    IOLog("ReleaseAll\n");
    
    if (device != NULL)
    {
        device->close(this);
        device = NULL;
    }
}

// Static wrapper for read notifications
void OneWirelessGamingReceiver::_ReadComplete(void *target, void *parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    IOLog("_ReadComplete\n");
    if (target != NULL)
        ((OneWirelessGamingReceiver*)target)->ReadComplete(parameter, status, bufferSizeRemaining);
}

// Static wrapper for write notifications
void OneWirelessGamingReceiver::_WriteComplete(void *target, void *parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    IOLog("_WriteComplete\n");
    if (target != NULL)
        ((OneWirelessGamingReceiver*)target)->WriteComplete(parameter, status, bufferSizeRemaining);
}

// Processes a message for a controller
void OneWirelessGamingReceiver::ProcessMessage(const unsigned char *data, int length)
{
    IOLog("ProcessMessage\n");
    const char hex[] = "0123456789ABCDEF";
//#ifdef PROTOCOL_DEBUG
    char s[1024];
    int i;
    
    for (i = 0; i < length; i++)
    {
        s[(i * 2) + 0] = hex[(data[i] & 0xF0) >> 4];
        s[(i * 2) + 1] = hex[data[i] & 0x0F];
    }
    s[i * 2] = '\0';
    IOLog("Got data (%d bytes): %s\n", length, s);
//#endif
    //IOLog("Got data (%d bytes)\n", length);
    
    //TODO: figure out if the buffer needs to be released, and how
}

// Get our location ID
OSNumber* OneWirelessGamingReceiver::newLocationIDNumber() const
{
    IOLog("NewLocationIDNumber\n");
    OSNumber *number;
    UInt32    location = 0;
    
    if (device)
    {
        if ((number = OSDynamicCast(OSNumber, device->getProperty("locationID"))))
        {
            location = number->unsigned32BitValue();
        }
        else
        {
            // Make up an address
            if ((number = OSDynamicCast(OSNumber, device->getProperty("USB Address"))))
                location |= number->unsigned8BitValue() << 24;
            
            if ((number = OSDynamicCast(OSNumber, device->getProperty("idProduct"))))
                location |= number->unsigned8BitValue() << 16;
        }
    }
    
    return OSNumber::withNumber(location, 32);
}

IOBufferMemoryDescriptor* OneWirelessGamingReceiver::GetFirmware(int index)
{
    char* buff;
    unsigned long hexLength;
    IOBufferMemoryDescriptor* outBuffer;
    
    switch (index) {
        case 1:
            hexLength = strlen(firmware1);
            buff = new char[hexLength];
            ::HexToBytes(firmware1, buff, hexLength);
            break;
        case 2:
            hexLength = strlen(firmware2);
            buff = new char[hexLength];
            ::HexToBytes(firmware2, buff, hexLength);
            break;
        case 3:
            hexLength = strlen(firmware3);
            buff = new char[hexLength];
            ::HexToBytes(firmware3, buff, hexLength);
            break;
        case 4:
            hexLength = strlen(firmware4);
            buff = new char[hexLength];
            ::HexToBytes(firmware4, buff, hexLength);
            break;
        case 5:
            hexLength = strlen(firmware5);
            buff = new char[hexLength];
            ::HexToBytes(firmware5, buff, hexLength);
            break;
        case 6:
            hexLength = strlen(firmware6);
            buff = new char[hexLength];
            ::HexToBytes(firmware6, buff, hexLength);
            break;
        case 7:
            hexLength = strlen(firmware7);
            buff = new char[hexLength];
            ::HexToBytes(firmware7, buff, hexLength);
            break;
        case 8:
            hexLength = strlen(firmware8);
            buff = new char[hexLength];
            ::HexToBytes(firmware8, buff, hexLength);
            break;
        case 9:
            hexLength = strlen(firmware9);
            buff = new char[hexLength];
            ::HexToBytes(firmware9, buff, hexLength);
            break;
        case 10:
            hexLength = strlen(firmware10);
            buff = new char[hexLength];
            ::HexToBytes(firmware10, buff, hexLength);
            break;
        default:
            break;
    }
    
    
    
    outBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, hexLength/2);
    outBuffer->writeBytes(0, buff, hexLength/2);
    
    return outBuffer;
}


