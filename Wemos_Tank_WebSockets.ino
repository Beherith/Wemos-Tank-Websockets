#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
//#include <ArduinoOTA.h>
//#include <WiFiUdp.h>

#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>
#include <Hash.h>

/* Set these to your desired credentials. */

#define DEBUGPIN 12

//#define DEBUG_ESP_HTTP_SERVER

const char *ssid = "TigerTank";
const char *password = "";//"TigerTank";

volatile unsigned long next;

const char* mdnsName = "tank"; // Domain name for the mDNS responder

const byte captive_portal=1;
const byte DNS_PORT = 53;
const char* serverIndex = "<form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

extern const char index_html[];

unsigned int alivecount=0;
unsigned long int lastpacket = 0;
bool active = 0;

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);


void handleRoot() {
 
  server.send_P(200,"text/html", index_html);
  Serial.println("handleroot: Client in.");
  }
 
void handle404() {
 
  server.send(200,"text/html", "<html><title>NO Tank</title><body>No Tank 404</body></html>");
  Serial.println("handle404");
  }
 
#define L1 0 //4
#define L2 14 //5
#define R1 4 //0
#define R2 5 //14
#define redled 12

int PwmFrequency = 20000;
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
			digitalWrite(redled,LOW);
			Serial.println("WS DC");
             break;
        case WStype_CONNECTED: {
            // send message to client
            webSocket.sendTXT(num, "Connected");
			digitalWrite(redled,HIGH);
			Serial.println("WS CONN");
        }
            break;
        case WStype_TEXT: {
            if(payload[0]=='g' && payload[1]=='e' && payload[2]=='t'){
/*              msp_analog_t analogdata;
              String senddata="{\"vbat\": ";
              if (msp.request(MSP_ANALOG, &analogdata, sizeof(analogdata))) {
                senddata+=String(analogdata.vbat);
              }
              else
 
               senddata+="0";
              senddata += "}";
              webSocket.sendTXT(num, senddata);
*/            }
			Serial.println("WS_Text");
        }
        break;
        case WStype_BIN: {
			if (length ==5){
				active = 1;
				analogWrite(L2,payload[0]   );
				analogWrite(L1,payload[1]   );
				analogWrite(R2,payload[2]   );
				analogWrite(R1,payload[3]  );
				if (payload[4] & 1) digitalWrite(redled,HIGH);
				else digitalWrite(redled,LOW);
				
				//we have 5 bits of pwmfreq;
				float pwmfactor = (payload[4] & 0b00111110)>>1;
				float newpwmfreq = (20000.0 * pow(0.82,pwmfactor));
				//Serial.println(pwmfactor);
				//Serial.println(newpwmfreq);
				int k = (float) newpwmfreq;
				if (k!= PwmFrequency){
					PwmFrequency = k;
					analogWriteFreq(PwmFrequency);
					
				}

				//50/20000 = y^31
				//
				
				lastpacket = millis();
				//Serial.print("lp: ");
			
				//Serial.println(lastpacket,HEX);
			}else{
				Serial.print("Binary packet length !=5: ");
				Serial.println(length);
			}
			for (int i = 0; i < length; i++){
				Serial.print(payload[i]);
			}
			//Serial.println(" <-packet");
        }
        break;
    }

}
void toggletest(int pin){
  for (int i = 0; i<32;i++){
    analogWrite(pin,8*i);
    delay(3);
  }
  analogWrite(pin,0);
  delay(500);
}
void setupmotorpins(){
  analogWriteRange(255);
  analogWriteFreq(PwmFrequency) ;
  // initialise pins
  pinMode(L1,OUTPUT);
  pinMode(L2,OUTPUT);
  pinMode(R1,OUTPUT);
  pinMode(R2,OUTPUT);
  pinMode(redled,OUTPUT);
  digitalWrite(L1,LOW);
  digitalWrite(L2,LOW);
  digitalWrite(R1,LOW);
  digitalWrite(R2,LOW);
  digitalWrite(redled,LOW);
  toggletest(L1);
  toggletest(L2);
  toggletest(R1);
  toggletest(R2);
  pinMode(BUILTIN_LED, OUTPUT); 
}

void startMDNS() { // Start the mDNS responder
  MDNS.begin(mdnsName);                        // start the multicast domain name server
  Serial.print("mDNS responder started: http://");
  Serial.print(mdnsName);
  Serial.println(".local");
}

void setup() {
	pinMode(DEBUGPIN,OUTPUT);
	setupmotorpins();
	//digitalWrite(DEBUGPIN, !onState); //set the PPM signal pin to the default state (off)
/* You can remove the password parameter if you want the AP to be open. */

//  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
	WiFi.softAP(ssid,password,2);

	IPAddress myIP = WiFi.softAPIP();

	if(captive_portal) dnsServer.start(DNS_PORT, "*", apIP);
	server.onNotFound(handle404);
	server.on("/", handleRoot);

	server.on("/update", HTTP_GET, [](){
		delay(100);    
		server.sendHeader("Connection", "close");
		server.sendHeader("Access-Control-Allow-Origin", "*");
		server.send(200, "text/html", serverIndex);
	});

	server.on("/upload", HTTP_POST, [](){
		server.sendHeader("Connection", "close");
		server.sendHeader("Access-Control-Allow-Origin", "*");
		server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
		ESP.restart();
	  },[](){
		  HTTPUpload& upload = server.upload();
		  if(upload.status == UPLOAD_FILE_START){
			WiFiUDP::stopAll();
			uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
			if(!Update.begin(maxSketchSpace)){//start with max available size
	 //         Update.printError(Serial);
			}
		  } 
		  else if(upload.status == UPLOAD_FILE_WRITE){
			if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
	//          Update.printError(Serial);
			}
		  } 
		  else if(upload.status == UPLOAD_FILE_END){
			if(Update.end(true)){ //true to set the size to the current progress
	 //         Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
			} else {
	//          Update.printError(Serial);
			}
	 //       Serial.setDebugOutput(false);
		  }
		  yield();
		});
	 
	server.begin();
	webSocket.onEvent(webSocketEvent);
	webSocket.begin();
	startMDNS();
	Serial.begin(115200);
	Serial.println("Tank Ready");
	Serial.println(pow(0.82,31.0));
}

unsigned long time_now = 0;

void loop() {

  webSocket.loop();
//  ArduinoOTA.handle();
  if(captive_portal)
    dnsServer.processNextRequest();
  server.handleClient();
  if (active && ( millis() - lastpacket>750)){
		Serial.println("Timeout after 300ms");
		analogWrite(L2,0  );
		analogWrite(L1,0  );
		analogWrite(R2,0  );
		analogWrite(R1,0  );
		active = 0;

  }

  yield();
}
