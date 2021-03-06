/*
 Copyright (c) 2016 Luis Claudio Gamboa Lopes <lcgamboa@yahoo.com>
 Copyright (c) 2014 tmrh20@gmail.com, github.com/TMRh20 

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "RF24Gateway.h"
#include "RF24Mesh/RF24Mesh_config.h"

/***************************************************************************************/

RF24Gateway::RF24Gateway()
{
}

/***************************************************************************************/

void RF24Gateway::begin(uint8_t nodeID, uint8_t _channel, rf24_datarate_e data_rate) {

    mesh_enabled = true;	  
	begin(true, mesh_enabled, 0, nodeID, data_rate, _channel);
}

/***************************************************************************************/

void RF24Gateway::begin(uint16_t address, uint8_t _channel, rf24_datarate_e data_rate, bool meshEnable, uint8_t nodeID) {
		 
	begin(0, mesh_enabled, address, nodeID, data_rate, _channel);
}

/***************************************************************************************/

bool RF24Gateway::begin(bool configTUN, bool meshEnable, uint16_t address, uint8_t mesh_nodeID, rf24_datarate_e data_rate, uint8_t _channel) {

    #if(DEBUG>=1)
		printf("GW Begin\n");
		printf("Config Device address 0%o nodeID %d\n",address,mesh_nodeID);
	#endif
	config_TUN = configTUN;
	
	///FIX
	
	channel = _channel;//97;
	
	dataRate = data_rate;
	
	configDevice(address);
    mesh_enabled = meshEnable;
	thisNodeID = mesh_nodeID;
	thisNodeAddress = address;

    if(meshEnable){
      // GW radio channel setting takes precedence over mesh_default_channel
	  if(channel == 97 && MESH_DEFAULT_CHANNEL != 97){
	    channel = MESH_DEFAULT_CHANNEL;
	  }
	
	  if(!thisNodeAddress && !mesh_nodeID){	  
	     RF24M_setNodeID(0);
	  }else{
		if(!mesh_nodeID){
			mesh_nodeID = 253;
		}
		RF24M_setNodeID(mesh_nodeID); //Try not to conflict with any low-numbered node-ids
	  }
	  RF24M_begin(channel,data_rate,MESH_RENEWAL_TIMEOUT);
	  thisNodeAddress = RF24M_getCurrentAddress();
	}else{
	  RF24_begin();
      delay(5);
      const uint16_t this_node = address;
	  RF24_setDataRate(dataRate);
	  RF24_setChannel(channel);
	  
          RF24N_begin(/*node address*/ this_node);
	  thisNodeAddress = this_node;
	  
	}
	RF24N_setMulticastRelay();


    //#if (DEBUG >= 1)
        RF24_printDetails();
    //#endif
    
    setupSocket();
	
	return true;
}

/***************************************************************************************/

bool RF24Gateway::meshEnabled(){
  return mesh_enabled;
}

/***************************************************************************************/

int RF24Gateway::configDevice(uint16_t address){

    std::string tunTapDevice = "tun_nrf24";
    strcpy(tunName, tunTapDevice.c_str());

	int flags;
	if(config_TUN){
      flags = IFF_TUN | IFF_NO_PI | IFF_MULTI_QUEUE;
	}else{
	  flags = IFF_TAP | IFF_NO_PI | IFF_MULTI_QUEUE;
    }
	tunFd = allocateTunDevice(tunName, flags, address);
	#if DEBUG >= 1
    if (tunFd >= 0) {
		std::cout << "RF24Gw: Successfully attached to tun/tap device " << tunTapDevice << std::endl;
    } else {
        std::cerr << "RF24Gw: Error allocating tun/tap interface: " << tunFd << std::endl;
        exit(1);
    }
	#endif
    return tunFd;
}

/***************************************************************************************/

int RF24Gateway::allocateTunDevice(char *dev, int flags, uint16_t address) {
    struct ifreq ifr;
    int fd;

    //open the device 
    if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
       return fd;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;   // IFF_TUN or IFF_TAP, plus maybe IFF_NO_PI

    if (*dev) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    // Create device
    if(ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
        //close(fd);
		//#if (DEBUG >= 1)
        std::cerr << "RF24Gw: Error: enabling TUNSETIFF" << std::endl;
		std::cerr << "RF24Gw: If changing from TAP/TUN, run 'sudo ip link delete tun_nrf24' to remove the interface" << std::endl;
        return -1;
		//#endif
    }

    //Make persistent
    if(ioctl(fd, TUNSETPERSIST, 1) < 0){
	    #if (DEBUG >= 1)
        std::cerr << "RF24Gw: Error: enabling TUNSETPERSIST" << std::endl;
		#endif
        return -1;
    }

	if(!config_TUN){
	  struct sockaddr sap;
      sap.sa_family = ARPHRD_ETHER;
      ((char*)sap.sa_data)[4]=address;
      ((char*)sap.sa_data)[5]=address>>8;
      ((char*)sap.sa_data)[0]=0x52;
      ((char*)sap.sa_data)[1]=0x46;
      ((char*)sap.sa_data)[2]=0x32;
      ((char*)sap.sa_data)[3]=0x34;
	
	  //printf("Address 0%o first %u last %u\n",address,sap.sa_data[0],sap.sa_data[1]);
      memcpy((char *) &ifr.ifr_hwaddr, (char *) &sap, sizeof(struct sockaddr));	

      if (ioctl(fd, SIOCSIFHWADDR, &ifr) < 0) {
        #if DEBUG >= 1
		fprintf(stderr, "RF24Gw: Failed to set MAC address\n");
		#endif
      }
	}

    strcpy(dev, ifr.ifr_name);
    return fd;
}

/***************************************************************************************/

int RF24Gateway::setIP( char *ip_addr, char *mask) {
	
	struct ifreq ifr;
	struct sockaddr_in sin;
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd == -1){
      fprintf(stderr, "Could not get socket.\n");
      return -1;
    }
 
     
    sin.sin_family = AF_INET;
	//inet_aton(ip_addr,&sin.sin_addr.s_addr);
	inet_aton(ip_addr,&sin.sin_addr);
	strncpy(ifr.ifr_name, tunName, IFNAMSIZ);
	
	if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
		fprintf(stderr, "ifdown: shutdown ");
		perror(ifr.ifr_name);
		return -1;
	}

	#ifdef ifr_flags
	# define IRFFLAGS       ifr_flags
	#else   /* Present on kFreeBSD */
	# define IRFFLAGS       ifr_flagshigh
	#endif

    if (!(ifr.IRFFLAGS & IFF_UP)) {
		//fprintf(stdout, "Device is currently down..setting up.-- %u\n",ifr.IRFFLAGS);
		ifr.IRFFLAGS |= IFF_UP;
		if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
			fprintf(stderr, "ifup: failed ");
			perror(ifr.ifr_name);
			return -1;
		}
	}	

	memcpy(&ifr.ifr_addr, &sin, sizeof(struct sockaddr)); 
 
  // Set interface address
  if (ioctl(sockfd, SIOCSIFADDR, &ifr) < 0) {
    fprintf(stderr, "Cannot set IP address. ");
    perror(ifr.ifr_name);
    return -1;
  }            


   inet_aton(mask, &sin.sin_addr);
   memcpy(&ifr.ifr_addr, &sin, sizeof(struct sockaddr));

   if (ioctl(sockfd, SIOCSIFNETMASK, &ifr) < 0)
   {
     fprintf(stderr,"Cannot define subnet mask for this device");
	 perror(ifr.ifr_name);
     return -1;
   }
 
 
   #undef IRFFLAGS
	return 0;
}

/***************************************************************************************/

void RF24Gateway::update(bool interrupts){
  
  if(interrupts){   
    handleRadioIn();
    handleTX();
  }else{
    handleRadioIn();
    handleTX();
    handleRX();
    handleRadioOut();
  }  
}

void RF24Gateway::poll(uint32_t waitDelay){

    handleRX(waitDelay);
    rfNoInterrupts();
    //gateway.poll() is called manually when using interrupts, so if the radio RX buffer is full, or interrupts have been missed, check for it here.
    if(RF24_rxFifoFull()){
      fifoCleared=true;
      update(true); //Clear the radio RX buffer & interrupt flags before relying on interrupts
    }
    handleRadioOut();
    rfInterrupts();
}
/***************************************************************************************/

void RF24Gateway::handleRadioIn(){
    
    if(mesh_enabled){
      while(RF24M_update());
      if(!thisNodeAddress){
          RF24M_DHCP();
    }
    }else{
        while(RF24N_update());
    }
       
    RF24NetworkFrame_ f;
		while(qsize(RF24N_getExternalQueue(),RF24N_getExternalQueue_c()) > 0 ){
			f = qfront(RF24N_getExternalQueue(),RF24N_getExternalQueue_c());

            msgStruct msg;

            unsigned int bytesRead = f.message_size;

            if (bytesRead > 0) {
				memcpy(&msg.message,&f.message_buffer,bytesRead);
				msg.size=bytesRead;
		
                #if (DEBUG >= 1)
                    std::cout << "Radio: Received "<< bytesRead << " bytes ... " << std::endl;
                #endif
                #if (DEBUG >= 3)
                    //printPayload(msg.getPayloadStr(),"radio RX");
					std::cout <<  "TunRead: " << std::endl;
					for(size_t i=0; i<msg.size;i++){
						//std::cout << std::hex << buffer[i];
						printf(":%0x :",msg.message[i]); 
					}
					std::cout <<  std::endl;
					
                #endif
		
                rxQueue.push(msg);

            } else {
                //std::cerr << "Radio: Error reading data from RF24_ Read '" << bytesRead << "' Bytes." << std::endl;
            }
			qpop(RF24N_getExternalQueue(),RF24N_getExternalQueue_c());
			
        }
}


void RF24Gateway::handleRadioOut(){

		bool ok = 0;
		
        while(!txQueue.empty() && qsize(RF24N_getExternalQueue(),RF24N_getExternalQueue_c()) == 0) {
			
            
            msgStruct *msgTx = &txQueue.front();
			
            #if (DEBUG >= 1)
                std::cout << "Radio: Sending " << msgTx->size << " bytes ... ";
				std::cout << std::endl;
            #endif
            #if (DEBUG >= 3)
                //PrintDebug == 1 does not have an endline.
                //printPayload(msg.getPayloadStr(),"radio TX");
            #endif

			std::uint8_t *tmp = msgTx->message;
			

		  if(!config_TUN ){ //TAP can use RF24Mesh for address assignment, but will still use ARP for address resolution
		
			uint32_t RF24_STR = 0x34324652; //Identifies the mac as an RF24 mac
			uint32_t ARP_BC = 0xFFFFFFFF;   //Broadcast address
			struct macStruct{				
				uint32_t rf24_Verification;
				uint16_t rf24_Addr;
			};
			
		
			macStruct macData;
			memcpy(&macData.rf24_Addr,tmp+4,2);
			memcpy(&macData.rf24_Verification,tmp,4);
			

			if(macData.rf24_Verification == RF24_STR){
				const uint16_t other_node = macData.rf24_Addr;			
				RF24NetworkHeader header;
				RF24NH_init(&header,/*to node*/ other_node, EXTERNAL_DATA_TYPE);
				ok = RF24N_write_m(&header,&msgTx->message,msgTx->size);

			}else
			if(macData.rf24_Verification == ARP_BC){
				RF24NetworkHeader header;
				RF24NH_init(&header,/*to node*/ 00, EXTERNAL_DATA_TYPE); //Set to master node, will be modified by RF24Network if multi-casting
			  if(msgTx->size <= 42){	
				if(thisNodeAddress == 00){ //Master Node
				
				    uint32_t arp_timeout = millis();
					
					ok=RF24N_multicast(&header,&msgTx->message,msgTx->size,1 ); //Send to Level 1
					while(millis() - arp_timeout < 5){RF24N_update();}
					RF24N_multicast(&header,&msgTx->message,msgTx->size,1 ); //Send to Level 1					
					arp_timeout=millis();
					while(millis()- arp_timeout < 15){RF24N_update();}
					RF24N_multicast(&header,&msgTx->message,msgTx->size,1 ); //Send to Level 1					

				}else{

					ok = RF24N_write_m(&header,&msgTx->message,msgTx->size);					
				}
			  }
			}
		  }else{ // TUN always needs to use RF24Mesh for address assignment AND resolution

			   uint8_t lastOctet = tmp[19];
			   uint16_t meshAddr;

			  if ( (meshAddr = RF24M_getAddress(lastOctet)) > 0 || thisNodeID) {
				RF24NetworkHeader header;
				RF24NH_init(&header,meshAddr, EXTERNAL_DATA_TYPE);
			    if(thisNodeID){ //If not the master node, send to master (00)
				  header.to_node = 00;				
				}
			    ok = RF24N_write_m(&header, msgTx->message, msgTx->size);
			  }else{
				//printf("Could not find matching mesh nodeID for IP ending in %d\n",lastOctet);
			  }
		  
		  }
          //delay( rf24_min(msgTx->size/48,20));
		  txQueue.pop();
		  

			//printf("Addr: 0%#x\n",macData.rf24_Addr);
			//printf("Verif: 0%#x\n",macData.rf24_Verification);
            if (ok) {
               // std::cout << "ok." << std::endl;
            } else {
               // std::cerr << "failed." << std::endl;
            }
			
			

        } //End Tx
		
}

/***************************************************************************************/

void RF24Gateway::handleRX(uint32_t waitDelay){

    fd_set socketSet;
    struct timeval selectTimeout;
    uint8_t buffer[MAX_PAYLOAD_SIZE];
    int nread;

    FD_ZERO(&socketSet);
    FD_SET(tunFd, &socketSet);

    selectTimeout.tv_sec = 0;
    selectTimeout.tv_usec = waitDelay*1000;

    if (select(tunFd + 1, &socketSet, NULL, NULL,&selectTimeout) != 0) {
      if (FD_ISSET(tunFd, &socketSet)) {
        if ((nread = read(tunFd, buffer, MAX_PAYLOAD_SIZE)) >= 0) {

            #if (DEBUG >= 1)
              std::cout << "Tun: Successfully read " << nread  << " bytes from tun device" << std::endl;
            #endif
            #if (DEBUG >= 3)
		      std::cout <<  "TunRead: " << std::endl;
			  for(int i=0; i<nread;i++){
			    printf(":%0x :",buffer[i]); 
			  }
			std::cout <<  std::endl;
            #endif
            rfNoInterrupts();
		    msgStruct msg;
		    memcpy(&msg.message,&buffer,nread);
		    msg.size = nread;
			if(txQueue.size() < 2){
			  txQueue.push(msg);
			}else{
			  droppedIncoming++;
			}
            rfInterrupts();
		} else{
          #if (DEBUG >= 1)
	      std::cerr << "Tun: Error while reading from tun/tap interface." << std::endl;
	      #endif

	    }
      }
    }
}

/***************************************************************************************/

 void RF24Gateway::handleTX(){

		if(rxQueue.size() < 1){
          rfInterrupts();
		  return;
		}
		msgStruct *msg = &rxQueue.front();

        if(msg->size > MAX_PAYLOAD_SIZE){
            rfInterrupts();
			//printf("*****WTF OVER *****");
			rxQueue.pop();
			return;
		} 
			
        if (msg->size > 0  ) {

            size_t writtenBytes = write(tunFd, &msg->message, msg->size);
            if (writtenBytes != msg->size) {
                //std::cerr << "Tun: Less bytes written to tun/tap device then requested." << std::endl;
				#if DEBUG >= 1
				printf("Tun: Less bytes written %d to tun/tap device then requested %d.",writtenBytes,msg->size);
				#endif

            } else {
                #if (DEBUG >= 1)
                    std::cout << "Tun: Successfully wrote " << writtenBytes  << " bytes to tun device" << std::endl;
                #endif
            }
			
            #if (DEBUG >= 3)
                //printPayload(msg.message,"tun write");
				std::cout <<  "TunRead: " << std::endl;
				for(size_t i=0; i<msg->size;i++){
					//printf(":%0x :",msg->message[i]); 
				}
				std::cout <<  std::endl;				
            #endif
		rxQueue.pop();
        }

}

/***************************************************************************************/

void printPayload(std::string buffer, std::string debugMsg = "") {

}

/***************************************************************************************/

void printPayload(char *buffer, int nread, std::string debugMsg = "") {


}

/***************************************************************************************/
   
void RF24Gateway::setupSocket(){
    
  int ret;
  const char* myAddr = "127.0.0.1";
  
  addr.sin_family = AF_INET;
  ret = inet_aton(myAddr, &addr.sin_addr);
  if (ret == 0) { perror("inet_aton"); exit(1); }
  addr.sin_port = htons(32001);
  //buf = "Hello UDP";
  s = socket(PF_INET, SOCK_DGRAM, 0);
  if (s == -1) { perror("socket"); exit(1); }
}

/***************************************************************************************/

void RF24Gateway::sendUDP(uint8_t nodeID,RF24NetworkFrame_ frame){
    
  uint8_t buffer[MAX_PAYLOAD_SIZE+11];
  
  memcpy(&buffer[0], &nodeID,1);
  memcpy(&buffer[1],&frame.header,8);
  memcpy(&buffer[9],&frame.message_size,2);
  memcpy(&buffer[11],&frame.message_buffer,frame.message_size);

  int ret = sendto(s, &buffer, frame.message_size+11, 0, (struct sockaddr *)&addr, sizeof(addr));
  if (ret == -1) { perror("sendto"); exit(1); }
}

/***************************************************************************************/











