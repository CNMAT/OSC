
/**
 * A print class for testing the encoder
 */
class TestPrint : public Print {
  
  private: 
    //a small test buffer
    uint8_t buffer[64];
    
    //pointer to the current write spot
    int bufferPointer;
  
  public: 
  
    TestPrint(){
      bufferPointer = 0; 
    }
   
    size_t write(uint8_t character) {
      buffer[bufferPointer++] = character;
      return character;
    }
    
    int size(){
      return bufferPointer; 
    }

    uint8_t at(int index){
      return buffer[index]; 
    }
    
    void clear(){
      bufferPointer = 0; 
    }
};
