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
    
    IOLog("OneWirelessGamingReceiver::start\n");
    
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
    
    started = false;
    
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
                    else if (pipe->GetEndpointNumber() == 4)
                    {
                        //not sure what exactly this pipe is for.
                        //it could be for all controllers, or just the first controller detectedd
                        inCPipe = pipe;
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
    if (!controlOut(6, 0, 0x13a0, "3c3c3c3c")) goto fail;
    if (!controlOut(6, 0, 0x13a4, "3c3c3c3c")) goto fail;
    if (!controlOut(6, 0, 0x13a8, "000a1622")) goto fail;
    if (!controlOut(6, 0, 0x13ac, "760a1622")) goto fail;
    if (!controlOut(6, 0, 0x13b0, "18183f3f")) goto fail;
    if (!controlOut(6, 0, 0x13c0, "06060080")) goto fail;
//next: 344
    if (!controlOut(6, 0, 0x13e0, "0420f5e3")) goto fail;
    if (!controlOut(6, 0, 0x13e4, "8420f5e3")) goto fail;
    if (!controlOut(6, 0, 0x13e8, "0421f5e3")) goto fail;
    if (!controlOut(6, 0, 0x13ec, "ff0f0600")) goto fail;
    if (!controlOut(6, 0, 0x1400, "9f5f0100")) goto fail;
    if (!controlOut(6, 0, 0x1408, "7f010000")) goto fail;
    if (!controlOut(6, 0, 0x140c, "03800000")) goto fail;
    if (!controlOut(6, 0, 0x150c, "02000000")) goto fail;
    if (!controlOut(6, 0, 0x1608, "02000000")) goto fail;
    if (!controlOut(6, 0, 0x13e0, "0420f4e3")) goto fail;
    if (!controlOut(6, 0, 0x13e4, "8420f4e3")) goto fail;
    if (!controlOut(6, 0, 0x13e8, "0421f4e3")) goto fail;
    if (!controlOut(6, 0, 0x1264, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x1228, "000000ee")) goto fail;
    if (!controlOut(6, 0, 0x122c, "000000ee")) goto fail;
    if (!controlOut(6, 0, 0x13a0, "3c3c3c0f")) goto fail;
    if (!controlOut(6, 0, 0x13a4, "3c3c3c0f")) goto fail;
    if (!controlOut(6, 0, 0x0404, "f5bcfe1e")) goto fail;
    if (!controlOut(6, 0, 0x0a38, "0a000000")) goto fail;
    if (!controlOut(6, 0, 0x0504, "0000007f")) goto fail;
    if (!controlOut(6, 0, 0x050c, "0000801a")) goto fail;
    
    for (UInt16 ctraddr = 0xa800; ctraddr <= 0xabfc; ctraddr+= 4)
    {
        IOSleep(10);
        if (!controlOut(6, 0, ctraddr, "01000000")) goto fail;
    }
    
//next:1175
    if (!controlIn (7, 0, 0x1100, "0a10a433")) goto fail;
    if (!controlOut(6, 0, 0x1100, "0a0ea433")) goto fail;
    if (!controlOut(6, 0, 0x041c, "00081018")) goto fail;
    if (!controlOut(6, 0, 0x0420, "20283038")) goto fail;
    if (!controlOut(6, 0, 0x0424, "40485058")) goto fail;
    if (!controlOut(6, 0, 0x0428, "60687078")) goto fail;
    if (!controlIn (7, 0, 0x080c, "3302ff03")) goto fail;
    if (!controlOut(6, 0, 0x080c, "2302ff03")) goto fail;
    if (!controlIn (7, 0, 0x1344, "202b0900")) goto fail;
    if (!controlOut(6, 0, 0x1344, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x1200, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x7028, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x7010, "00000000")) goto fail;
    
    //return true;
    IOSleep(100);
    for (UInt16 ctraddr = 0x1800; ctraddr <= 0x1fe8; ctraddr+= 8)
    {
        IOSleep(10);
        if (!controlOut(6, 0, ctraddr, "ffffffffffff0000")) goto fail;
    }
    //a check?
    if (!controlIn (7, 0, 0x1114, "40060000")) goto fail;
//next:1979
    if (!controlOut(6, 0, 0x1114, "40060000")) goto fail;
    if (!controlIn (7, 0, 0x1700, "02000000")) goto fail;
    if (!controlIn (7, 0, 0x1704, "a20a0100")) goto fail;
    if (!controlIn (7, 0, 0x1708, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x170c, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x1710, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x1714, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x1340, "3f580000")) goto fail;
    
    
    if (!controlIn (7, 0, 0x0024, "0388c081")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0388c081")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "038800c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "18880080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "6245b4ea")) goto fail;
    if (!controlIn (7, 0, 0x0024, "18880080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "188800c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "18880080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "6245b4ea")) goto fail;
    if (!controlIn (7, 0, 0x0024, "18880080")) goto fail;
//next:2033
    if (!controlOut(6, 0, 0x0024, "188800c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "18880080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "f7580000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "18880080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "188830c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "15883080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "12ff0200")) goto fail;
    if (!controlIn (7, 0, 0x0024, "15883080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "158830c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "15883080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "12ff0200")) goto fail;
    if (!controlIn (7, 0, 0x0024, "15883080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "1588f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "ffff1410")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "ffff1410")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "14100000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
//next:2108
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "14100000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "00ffffff")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "00ffffff")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x0034, "ffffffff")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
//next:2153
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x0034, "ffffffff")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d8830c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "15883080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "ffff2700")) goto fail;
    if (!controlIn (7, 0, 0x0024, "15883080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "158890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    //this diverges!!!! from a 3'rd party scan: 0000002d
    //from my scan: 00000034
    //don't know if these are machine based, or different on each scan..
    //so we'll do nothing if values doesn't match..
    controlIn (7, 0, 0x0034, "00000034");
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "108890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    //same as before
    controlIn (7, 0, 0x0034, "00000034");
    
    if (!controlIn (7, 0, 0x0114, "00000000")) goto fail;
    //this is specific, needs to be the previous value (just one byte?)
    if (!controlOut(6, 0, 0x0114, "00340000")) goto fail;
    if (!controlIn (7, 0, 0x0118, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x0118, "007f0000")) goto fail;
    if (!controlIn (7, 0, 0x0020, "ffe79100")) goto fail;
    if (!controlOut(6, 0, 0x0020, "ffa79100")) goto fail;
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "108850c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "1f00c1c1")) goto fail;
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
//next:2231
    if (!controlOut(6, 0, 0x0024, "1b8850c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "1f00c1c1")) goto fail;
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "1b8850c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    if (!controlIn (7, 0, 0x0034, "00002600")) goto fail;
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "1b8860c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "138860c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "27000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "138860c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "27000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "138860c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    //diverges compared to a 3rd party scan: 00290081
    controlIn (7, 0, 0x0030, "00280081");
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "138860c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "00280081")) goto fail;
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
//2315
    if (!controlOut(6, 0, 0x0024, "138860c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    if (!controlIn (7, 0, 0x0034, "00002900")) goto fail;
    if (!controlIn (7, 0, 0x0024, "13886080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "138870c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "00000027")) goto fail;
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "128870c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "00000027")) goto fail;
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "128870c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
//next:2363
    if (!controlOut(6, 0, 0x0024, "128870c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "27000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "128870c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "27000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "128870c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    //this diverges, 3rd party scan: 00240000
    controlIn (7, 0, 0x0034, "00220082");
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "128870c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    //same as before
    controlIn (7, 0, 0x0034, "00220082");
    if (!controlIn (7, 0, 0x0024, "12887080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "128880c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "00002700")) goto fail;
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
//next:2423
    if (!controlOut(6, 0, 0x0024, "118880c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "0000002b")) goto fail;
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "118880c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "0000002b")) goto fail;
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "118880c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "118880c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    if (!controlIn (7, 0, 0x0034, "29000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
//next:2471
    if (!controlOut(6, 0, 0x0024, "118880c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    if (!controlIn (7, 0, 0x0034, "29000000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "11888080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "118890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "00270000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "118890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "00270000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "108890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "00002700")) goto fail;
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
//next:2519
    if (!controlOut(6, 0, 0x0024, "108890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "00000026")) goto fail;
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "108890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    if (!controlIn (7, 0, 0x0030, "00000026")) goto fail;
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "108890c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    //diverges, 3rd party scan: 0000002d
    controlIn (7, 0, 0x0034, "00000034");
    if (!controlIn (7, 0, 0x0024, "10889080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "1088f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "ffff1410")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "0d88f0c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
    if (!controlIn (7, 0, 0x002c, "14100000")) goto fail;
    if (!controlIn (7, 0, 0x0024, "0d88f080")) goto fail;
//next:2579 ->(in test_no_press: 2695)
    if (!controlOut(6, 0, 0x0024, "0d8850c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    //diverges, 3rd party scan: 22bc0000
    controlIn (7, 0, 0x002c, "22b60000");
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    
    if (!controlOut(6, 0, 0x0024, "1b8850c0")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    if (!controlIn (7, 0, 0x0028, "00000022")) goto fail;
    
    if (!controlIn (7, 0, 0x2714, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x2714, "00000000")) goto fail;
    if (!controlIn (7, 0, 0x293c, "1c1bf2c8")) goto fail;
    if (!controlOut(6, 0, 0x293c, "181bf2c8")) goto fail;
//next:2612
    //THIS IS NOT ACTUAL FIRMWARE!!!
    //these section diverges between 3rd party scans!!!!
    if (!QueueWrite(outDevicePipe, GetFirmware(11)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 11\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware(12)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 12\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);
    
//next:2615
    if (!controlIn (7, 0, 0x0080, "0f020000")) goto fail;
    if (!controlIn (7, 0, 0x141c, "e4ff0000")) goto fail;
    if (!controlOut(6, 0, 0x141c, "e4f00000")) goto fail;
    if (!controlIn (7, 0, 0x110c, "1e000000")) goto fail;
    if (!controlOut(6, 0, 0x110c, "1e000000")) goto fail;
    if (!controlIn (71, 0, 0x0120, "00000000")) goto fail;
    if (!controlOut(70, 0, 0x0120, "10100000")) goto fail;
    if (!controlIn (71, 0, 0x0128, "00000000")) goto fail;
    if (!controlOut(70, 0, 0x0128, "00100000")) goto fail;
    if (!controlIn (71, 0, 0x01b8, "fffff7ff")) goto fail;
    if (!controlOut(70, 0, 0x01b8, "f5fff7ff")) goto fail;
    
    //NOT ACTUAL FIRMWARE
    if (!QueueWrite(outDevicePipe, GetFirmware(13)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);

//next:2651
    if (!controlIn (7, 0, 0x1340, "3f580000")) goto fail;
    if (!controlOut(6, 0, 0x1340, "3f581000")) goto fail;
    if (!controlIn (7, 0, 0x110c, "1e000000")) goto fail;
    if (!controlOut(6, 0, 0x110c, "5f010000")) goto fail;
    if (!controlIn (7, 0, 0x141c, "e4f00000")) goto fail;
    if (!controlIn (7, 0, 0x2308, "14140000")) goto fail;
    
//next:2669
    //NOT ACTUAL FIRMWARE
    if (!QueueWrite(outDevicePipe, GetFirmware(14)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware(15)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware(16)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware(17)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware(18)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    waitWriteCompleted();
    
    //read the response?
    QueueRead(inDevicePipe);

//next:2678
    if (!controlIn (7, 0, 0x1104, "09010000")) goto fail;
    if (!controlOut(6, 0, 0x1104, "09010000")) goto fail;
    if (!controlOut(6, 0, 0x1004, "04000000")) goto fail;
    if (!controlOut(70, 0, 0x9018, "1838e400")) goto fail;
    if (!controlIn (71, 0, 0x9018, "1838e400")) goto fail;
    if (!controlIn (7, 0, 0x1004, "04000000")) goto fail;
    if (!controlOut(6, 0, 0x1400, "177f0100")) goto fail;
    if (!controlOut(6, 0, 0x1004, "0c000000")) goto fail;
    if (!controlOut(70, 0, 0x9018, "1838e400")) goto fail;
    if (!controlIn (71, 0, 0x9018, "1838e400")) goto fail;
    if (!controlIn (7, 0, 0x1004, "0c000000")) goto fail;
    if (!controlOut(6, 0, 0x1004, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x1330, "01101000")) goto fail;
    if (!controlOut(6, 0, 0x1334, "00000100")) goto fail;
    if (!controlOut(6, 0, 0x13a0, "3c3c3c0f")) goto fail;
    if (!controlOut(6, 0, 0x13a4, "3c3c3c0f")) goto fail;
    if (!controlIn (7, 0, 0x2378, "06000000")) goto fail;
    if (!controlOut(6, 0, 0x2378, "02000000")) goto fail;
    if (!controlIn (7, 0, 0x13c0, "06060080")) goto fail;
    if (!controlOut(6, 0, 0x13c0, "06060000")) goto fail;
    if (!controlIn (7, 0, 0x200c, "0a030087")) goto fail;
    if (!controlOut(6, 0, 0x200c, "0a030086")) goto fail;
    if (!controlOut(6, 0, 0x0504, "00000000")) goto fail;
    if (!controlOut(6, 0, 0x050c, "00000000")) goto fail;
    
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    if (!controlOut(6, 0, 0x0023, "1b8850c0")) goto fail;
    if (!controlIn (7, 0, 0x0024, "1b885080")) goto fail;
    //diverges from a 3rd party scan:22bc0000
    if (!controlIn (7, 0, 0x002c, "22b60000")) goto fail;
    
//next:2763 (2846)
    if (!QueueWrite(outDevicePipe, GetFirmware("0800fc51020000000000000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("0800fd51030000000100000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("0800fe51040000000000000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!controlOut(6, 0, 0x1004, "0c000000")) goto fail;
    
    //this diverges from 3rd party scans
    if (!QueueWrite(outDevicePipe, GetFirmware("1400e151010000000101000000000000000000000020011000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //also diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1400e25124000000010100000000000000000000012a011000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("1400e35134000000010100000000000000000000012b011000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("0800f451020000000000000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);

    if (!QueueWrite(outDevicePipe, GetFirmware("1400e551640000000101000000000000000000000227011000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1000a650b4134100207b3ceda8134100e08520f400000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("1400e751780000000101000000000000000000000227011000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("1000c850b41341000100a489a813410001040f1b00000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1400e951950000000101000000000000000000000222011000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1400ea51010000000101000000000000000000000020010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("08003b50060000004000000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("08008c501411410040066ff700000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("30000805a000002000ff1c00000000000000000000000001a0000000ffffffffffff6245b4ea2d596245b4ea2d5910000200010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("08008d501411410040066ff700000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("30000805a000002000ff1c00000000000000000000000001a0000000ffffffffffff6245b4ea2d596245b4ea2d5920000200010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("0c008e50081041006245b4ea2d59000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("0c008150101041006245b4ea2d59000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("0c003250000000006245b4ea2d59000000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    if (!QueueWrite(outDevicePipe, GetFirmware("0800835000144100134f010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1400e451010000000101000000000000000000000020010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffff3000000001080c1218243048606c00000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1800a55030114100a06cefe3341141000000000040114100b81c010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1800a65030114100a06cefe3341141000000000040114100b81c010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1400e751060000000101000000000000000000000022010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffff4000000001080c1218243048606c00000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1800a85030114100a06cefe334114100f07a20f4401141000101010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1800a95030114100a06cefe334114100f07a20f4401141000101010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
//next:2837 (2919) (we might have missed some reads, was too complicated to track..)
    
    //diverges
    if (!QueueWrite(outDevicePipe, GetFirmware("1400ea510b0000000101000000000000000000000022010000000000")))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        goto fail;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    
    //only for logging
    IOSleep(100);
// next optimized for smaller code:
    //consider diverges as default
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffff5000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800ab5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800ac5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1400ed5124000000010100000000000000000000012a010000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffff6000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800ae5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800a15030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1400e25128000000010100000000000000000000012a000000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffff7000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800a35030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800a45030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1400e5512c0000000101000000000000000000000129010000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffff8000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800a65030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800a75030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1400e851300000000101000000000000000000000129000000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffff9000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800a95030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800aa5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1400eb51950000000101000000000000000000000222010000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffffa000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800ac5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    //vs 3d party:------>1800a75030114100d0c2b2053411410060c9900d401141000000000000000000
    if (!QueueWriteRead("1800ad5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1400ee51990000000101000000000000000000000222000000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffffb000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800a15030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800a25030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    //only for logging:
    IOSleep(100);
//next: 2881? (double check) (2967)
    //does not diverge
    if (!QueueWriteRead("0800f351020000000000000000000000")) goto fail;
    if (!QueueWriteRead("1000a450b4134100a06cefe3a8134100f07a20f400000000")) goto fail;
    if (!QueueWriteRead("1000c550b41341000100a489a813410001040f1b00000000")) goto fail;
    if (!QueueWriteRead("1400e6519d0000000101000000000000000000000220010000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffffc000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800a75030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800a85030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    
    if (!QueueWriteRead("1400e951a10000000101000000000000000000000220000000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffffd000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800aa5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800ab5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("38000805a000002000ff240000000000000000000000000140000000ffffffffffff6245b4ea2d59ffffffffffffe000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("1800ac5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("1800ad5030114100a06cefe334114100f07a20f4401141000101010000000000")) goto fail;
    if (!QueueWriteRead("08008e5000144100177f010000000000")) goto fail;
    if (!QueueWriteRead("1400e151990000000101000000000000000000000222010000000000")) goto fail;
    if (!QueueWriteRead("080082501c04410000c0000000000000")) goto fail;
    if (!QueueWriteRead("5000835000c04100080000200200380000000000000000000000000080000000ffffffffffff6245b4ea2d596245b4ea2d59000088006cf700f8ffff640031c60000dd100050f211011000289d2400000000000000000000")) goto fail;
    if (!QueueWriteRead("080084501411410040065fec00000000")) goto fail;
    if (!QueueWriteRead("1800a55030114100a06cefe3341141000000000040114100c41c010000000000")) goto fail;
    if (!QueueWriteRead("1800a65030114100a06cefe3341141000000000040114100c41c010000000000")) goto fail;
    if (!QueueWriteRead("08008750240441004048505800000000")) goto fail;
    if (!QueueWriteRead("3c00885000d24100000000200200220000000000000000000000000070000000ffffffffffff6245b4ea2d596245b4ea2d590000700f1000289d00000000000000000000")) goto fail;
    if (!QueueWriteRead("08008950240441004048505800000000")) goto fail;
    if (!QueueWriteRead("3c008a5000d04100000000200200240000000000000000000000000040000000ffffffffffff6245b4ea2d59ffffffffffff0000000001080c1218243048606c00000000")) goto fail;
    if (!QueueWriteRead("38003b500700000001000000990000000a00000001000000060000000b00000024000000280000002c00000030000000950000009d000000a100000000000000")) goto fail;
    //for some reason needs a 200msec sleep
    IOSleep(200);
    if (!QueueWriteRead("0800fc51020000000000000000000000")) goto fail;
    if (!QueueWriteRead("1000ad50b4134100a06cefe3a8134100f07a20f400000000")) goto fail;
//next:2938 (3021)
    
    //IOSleep(500);
    //does not diverge
    if (!QueueWriteRead("1000ce50b41341000100a489a813410001040f1b00000000")) goto fail;
    IOSleep(100);
    
    IOLog("Started\n");
    started = true;
    //we should be receiving a 155 byte packet with 128 byte payload in 0.9 sec with the 4 starting bytes 7880a04a
    //meaning it has read all the wireless broadcasting devices ( or that it's ready to receive pairing?)

    //
    
    //reading from these pipes will be repetitive since we have set the started property to true.
    //QueueRead(inDevicePipe);
    QueueRead(inCPipe);
    //IOSleep(2000);
    
    return true;
    
fail:
    IOLog("fail\n");
    ReleaseAll();
    return false;
}

bool OneWirelessGamingReceiver::QueueWriteRead(const char payload[])
{
    if (!QueueWrite(outDevicePipe, GetFirmware(payload)))
    {
        //did not succeed!
        IOLog("Failed to write firmware 13\n");
        
        return false;;
    }
    
    //read the response?
    QueueRead(inDevicePipe);
    waitReadCompleted();
    
    return true;
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
            IOLog("Expected control value did not match! Retrying.\n");
        }
    }
    //TODO: compare inBuff with checkControl and print an error message plus return false
    return true;
}

bool OneWirelessGamingReceiver::controlOut(UInt8 bRequest, UInt16 wValue, UInt16 wIndex, const char controlHex[])
{
    unsigned char outBuff[256];
    IOUSBDevRequest request;
    int err;
    
    size_t inSize = 0;
    
    if (controlHex != 0)
    {
        inSize = strlen(controlHex);
        HexToBytes(controlHex, outBuff, inSize);
    }
    
    bzero( &request, sizeof(IOUSBDevRequest));
    
    request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    request.bRequest = bRequest;
    request.wValue = wValue;
    request.wIndex = wIndex;
    if (controlHex != 0)
    {
        request.wLength = inSize/2;
        request.pData = outBuff;
    }
    err = device->DeviceRequest(&request, 5000, 0);
    if (controlHex !=0)
    {
        //IOLog("CONTROL Out status: %d : %02x%02x%02x%02x\n", err, outBuff[0], outBuff[1], outBuff[2], outBuff[3]);
    }
    else
    {
        //IOLog("CONTROL Out status: %d\n", err);
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
    //we should read recursively only if we already started
    bool reread = started;
    IOUSBPipe *pipe = data != 0 ? data->pipe : 0;
    
    switch (status)
    {
        case kIOReturnOverrun:
            IOLog("read - kIOReturnOverrun, clearing stall\n");
            //TODO: figure out what to do!
            //connections[data->index].controllerIn->ClearStall();
            // fall through
            break;
        case kIOReturnSuccess:
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
    
    if (reread)
    {
        IOLog("Re-read set: %d\n", pipe);
    }
    //    QueueRead(data->pipe);
}

bool OneWirelessGamingReceiver::QueueWrite(IOUSBPipe *pipe, IOBufferMemoryDescriptor *outBuffer)
{
    //IOLog("QueueWrite\n");
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
    //IOLog("WriteComplete\n");
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
    //IOLog("_ReadComplete\n");
    if (target != NULL)
        ((OneWirelessGamingReceiver*)target)->ReadComplete(parameter, status, bufferSizeRemaining);
}

// Static wrapper for write notifications
void OneWirelessGamingReceiver::_WriteComplete(void *target, void *parameter, IOReturn status, UInt32 bufferSizeRemaining)
{
    //IOLog("_WriteComplete\n");
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
    //0400404A - button press on the wireless adapter
    if (data[0] == 0x04 && data[1] == 0x00 && data[2] == 0x40 && data[3] == 0x4A)
    {
        IOLog("Received adapter button press (fireworks, rainbows and unicorns)");
        QueueWrite(outDevicePipe, GetFirmware("040000510000000000000000"));
        QueueWrite(outDevicePipe, GetFirmware("080081501411410040060f0000000000"));
        QueueRead(inDevicePipe);
        QueueWrite(outDevicePipe, GetFirmware("1800825038c041000000dd100050f2110110019d289900000000000000000000"));
        QueueWrite(outDevicePipe, GetFirmware("080083501411410040063f6400000000"));
        
    }
    else if (data[0] == 0x48 && data[1] == 0x00 && data[2] == 0x08 && data[3] == 0x00)
    {
        //TODO: set a signal that we are pairing (so we don't send ping messages)?
        IOSleep(500);
        IOLog("Received pairing signal!!!! freaking unikorns shooting rainbows!");
        
        
    }
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
        case 11:
            hexLength = strlen(firmware11);
            buff = new char[hexLength];
            ::HexToBytes(firmware11, buff, hexLength);
            break;
        case 12:
            hexLength = strlen(firmware12);
            buff = new char[hexLength];
            ::HexToBytes(firmware12, buff, hexLength);
            break;
        case 13:
            hexLength = strlen(firmware13);
            buff = new char[hexLength];
            ::HexToBytes(firmware13, buff, hexLength);
            break;
        case 14:
            hexLength = strlen(firmware9);
            buff = new char[hexLength];
            ::HexToBytes(firmware9, buff, hexLength);
            break;
        case 15:
            hexLength = strlen(firmware10);
            buff = new char[hexLength];
            ::HexToBytes(firmware10, buff, hexLength);
            break;
        case 16:
            hexLength = strlen(firmware11);
            buff = new char[hexLength];
            ::HexToBytes(firmware11, buff, hexLength);
            break;
        case 17:
            hexLength = strlen(firmware12);
            buff = new char[hexLength];
            ::HexToBytes(firmware12, buff, hexLength);
            break;
        case 18:
            hexLength = strlen(firmware13);
            buff = new char[hexLength];
            ::HexToBytes(firmware13, buff, hexLength);
            break;
        default:
            break;
    }
    
    
    
    outBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, hexLength/2);
    outBuffer->writeBytes(0, buff, hexLength/2);
    
    return outBuffer;
}

IOBufferMemoryDescriptor* OneWirelessGamingReceiver::GetFirmware(const char input[])
{
    char* buff;
    unsigned long hexLength;
    IOBufferMemoryDescriptor* outBuffer;
    
    hexLength = strlen(input);
    buff = new char[hexLength];
    ::HexToBytes(input, buff, hexLength);
    outBuffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, hexLength/2);
    outBuffer->writeBytes(0, buff, hexLength/2);
    
    return outBuffer;
}



