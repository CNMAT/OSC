
/**
 * A print class for testing the encoder
 */
class TestPrint : public Print {
  
  private: 
    //a small test buffer
    uint8_t buffer[64];
    
    //pointer to the current write spot
    unsigned int bufferPointer;
  
  public: 
  
    TestPrint(){
      bufferPointer = 0; 
    }
   
    size_t write(uint8_t character) {
      buffer[bufferPointer++] = character;
      //Print::write() returns the number of bytes written, not the byte.
      //Returning the byte made every caller that checks the return value see a
      //bogus count -- and made a written 0x00 look like a failed write.
      return 1;
    }
    
    unsigned int size(){
      return bufferPointer; 
    }

    uint8_t at(int index){
      return buffer[index]; 
    }
    
    void clear(){
      bufferPointer = 0; 
    }
};
