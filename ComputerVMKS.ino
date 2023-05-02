#include <PS2Keyboard.h> // Keyboard library
#include <U8x8lib.h> // Display library

#define BUFFER_SIZE 64 // Size of the input buffer
#define STACK_SIZE 50 // Size of int stack
#define DEF_STACK_SIZE 10 // Size of the definition stack
#define IDX_STACK_SIZE (DEF_STACK_SIZE * 2) // Size of the stack of indexes for loops
#define SCREEN_SIZE 128 // Maximum symbols on the screen
#define BUZZER_PIN 6 // Digital pin used for the buzzer
#define BUZZER_SOUND 2000 // The frequency of the buzzer
#define BUZZER_DURATION 100 // Duration of the buzzer sound

const int dataPin = 5; // Data1 pin of keyboard module
const int irqPin = 3; // Clock1 pin of keyboard module
PS2Keyboard keyboard; // Keyboard object

U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE); // Display object
char screen[SCREEN_SIZE]; // All chars on screen

// Display cursor position
int sX = 0, sY = 0;

// Error types
typedef enum {
  NO_ERROR,                   // 0
  STACK_OVERFLOW,             //1
  STACK_UNDERFLOW,            // 2
  STACK_ERROR,                // 3
  INPUT_ERROR,                // 4
  EXPECTED_WORD,              // 5
  EXPECTED_QUOTE,             //6
  RECURSIVE_WORD_DEFINITION,  // 7
  LONE_THEN,                  //8
  OUTSIDE_WORD_DEFINITION,    // 9
  UNRECOGNIZED_WORD,          // 10
  INVALID_CONSTRUCTION,       // 11
  LONE_LOOP,                  // 12
  DIVISION_BY_ZERO,           // 13
  NODE_NULL_IN_IF,            // 14



} errorTypes;

errorTypes errorCode = NO_ERROR;

// Error messages
const char em0[]  =  "";                                           
const char em1[]  =  "S. overflow!";                                        
const char em2[]  =  "S. underflow!";                                  
const char em3[]  =  "S. error!";                                    
const char em4[]  = "Input error!";                                     
const char em5[]  = "Exp. a word!";                                
const char em6[]  = "\" expected!";                                     
const char em7[]  = "Recursive def!";               
const char em8[]  = "Lone THEN!";         
const char em9[]  = "Def only word!"; 
const char em10[]  = "Undefined word!";                               
const char em11[]  = "Invalid constr!";               
const char em12[]  = "Lone LOOP!";           
const char em13[]  = "Division by 0!";                           
const char em14[]  = "Null node!";  


const char welcome[]  = "8-bit Forth v0.1";

const char* const errorMessages[]  = {
  em0,                       
  em1,
  em2,
  em3,
  em4,
  em5,
  em6,
  em7,
  em8,
  em9,
  em10,
  em11,
  em12,
  em13,
  em14,
};

//Input buffer
char buffer[BUFFER_SIZE];
int b_idx = 0;
int b_end = 0;

// Definiton of word node so it can be used by all data structures
class wordNode;

// All word types

typedef enum {
  INTEGER,
  DOT,
  PLUS,
  MINUS,
  MULTIPLY,
  DIVIDE,
  PRINT_S,
  COLON,
  SEMICOLON,
  DEFINED_WORD,
  PRINT_STACK,
  IF,
  ELSE,
  THEN,
  EQUAL_TO,
  NOT_EQUAL_TO,
  LESS_THAN,
  MORE_THAN,
  LESS_THAN_ZERO,
  EQUAL_TO_ZERO,
  MORE_THAN_ZERO,
  DO,
  LOOP,
  I,
  CR,
  EMIT,
  DUP,
  MOD,
  SLASH_MOD,
  NEGATE,
  ABS,
  MAX,
  MIN,
  SWAP,
  ROT,
  DROP,
  NIP,
  TUCK,
  OVER,
  ROLL,
  PICK, 

} wordType;


// Working area - a value that a word can have. Stored in union so all values are stored in one place in the memory
typedef union {
  int intValue;
  const char* strValue;
  wordNode* nodeValue;

} workingArea;


// The class for a single word
class word_t {
public:
  wordType type;
  workingArea wa;
};

// Linked list of words
class wordNode {
public:
  word_t _word;
  wordNode* next;
};

// Linked list of defined words
class definedWordList {
public:
  const char* wordName;
  wordNode* startNode;
  definedWordList* next;
};

// List of strings that have already been detected from string printing
class string_list {
private:
  char* strValue;
  const string_list* next;
public:
  string_list(int idx, const string_list* next) {
    this->next = next;
    strValue = (char*)malloc(idx - b_idx + 1);
    strncpy(strValue, &buffer[b_idx], idx - b_idx);
    strValue[idx - b_idx] = 0;
  }
  const char* getStrValue() const {
    return strValue;
  }
  const string_list* getNext() const {
    return next;
  }
};

// parseWord definition so it can be used by all functions
void parseWord(int, word_t*);

// Integer stack
int stack[STACK_SIZE];
// points to first free location on top of the int stack
int top = 0;

// Stack for loops and if statement definitions
wordType defStack[DEF_STACK_SIZE];
// points to first free location on top of the defStack
int defTop = 0;
// Stack of indexes for loops. Each entry is 2 consequitive integers
int idxStack[IDX_STACK_SIZE];
// points to first free location on top of the loop idxStack
int idxTop = 0;

// Detects if a something is being printed so a new line should be printed before input or output that must start at beggining of a new line
bool printed = false;

string_list* s_list = NULL;

// True, if word definition is started but not ended yet, that is between a colon(:) and semi-colon(;)
bool inWordDefinition = false;

definedWordList* wordList = NULL;

// Compare a string to a part of the input buffer
bool compare(const char* s1, int s_idx, int e_idx) {
  int len = e_idx - s_idx;

  if (len != strlen(s1)) {
    return false;
  }

  return strncmp(s1, &buffer[s_idx], len) == 0;
}

// Pushes a value to the int stack
void push(int value) {
  if (errorCode != NO_ERROR)
    return;

  if (top >= STACK_SIZE) {
    errorCode = STACK_OVERFLOW;
    return;
  }

  stack[top++] = value;
}


// Discards count values from the int stack
void discard(int count) {
  if (errorCode != NO_ERROR)
    return;

  if (top < count) {
    errorCode = STACK_UNDERFLOW;
    return;
  }

  top -= count;
}


// Gets a value with offset idx from top of the int stack. Look also: poke() for write
// 0 - top of the stack, next push will write there
// idx > 0 - direction of items that are already in the stack
// idx < 0 - direction where pop'ed values still exist until overwriten by future push
int peek(int idx) {
  if (errorCode != NO_ERROR)
    return 1;

  idx = top - idx;

  if (idx < 0) {
    // Should never happen
    errorCode = STACK_UNDERFLOW;
    return 1;
  } else if (idx >= STACK_SIZE) {
    errorCode = STACK_OVERFLOW;
    return 1;
  }

  return stack[idx];
}

// Remove the top value from the int stack and return it as result
int pop() {
  if (errorCode != NO_ERROR)
    return 1;

  if (top <= 0) {
    errorCode = STACK_UNDERFLOW;
    return 1;
  }

  return stack[--top];
}

// Directly write to the stack relative to the current top position. Look also: peek() for read
void poke(int idx, int val) {
  if (errorCode != NO_ERROR)
    return;

  idx = top - idx;

  if (idx < 0) {
    // Should never happen
    errorCode = STACK_ERROR;
    return;
  }

  stack[idx] = val;
}

// Pushes a word type in the defStack
void defPush(wordType value) {
  if (errorCode != NO_ERROR)
    return;

  if (defTop >= DEF_STACK_SIZE) {
    errorCode = STACK_OVERFLOW;
    return;
  }

  defStack[defTop++] = value;
}

// Discards the top word type in the defStack
void defDiscard() {
  if (errorCode != NO_ERROR)
    return;

  if (defTop <= 0) {
    errorCode = STACK_UNDERFLOW;
    return;
  }

  --defTop;
}

// Gets the value on top of the defStack
wordType defPeek() {
  if (errorCode != NO_ERROR)
    return INTEGER;


  if (defTop < 1 || defTop >= STACK_SIZE) {
    // Should never happen
    errorCode = STACK_ERROR;
    return INTEGER;
  }

  return defStack[defTop - 1];
}

// Checks if there is a certain word type in the defStack
bool defHas(wordType type) {
  for (int i = 0; i < defTop; i++) {
    if (type == defStack[i]) {
      return true;
    }
  }

  return false;
}

// Push a single integer value in the loop idxStack. It should be called twice to create whole entry
void idxPush(int value) {
  if (errorCode != NO_ERROR)
    return;

  if (idxTop >= IDX_STACK_SIZE) {
    errorCode = STACK_OVERFLOW;
    return;
  }

  idxStack[idxTop++] = value;
}

// Discards 2 values from the idxStack. This is one loop entry in idxStack
void idxDiscard() {
  if (errorCode != NO_ERROR)
    return;

  if (idxTop < 2) {
    errorCode = STACK_UNDERFLOW;
    return;
  }

  defTop -= 2;
}

// Gets a value with offset from top of the idxStack
int idxPeek(int off) {
  if (errorCode != NO_ERROR)
    return 0;


  if (idxTop - off < 0) {
    // Should never happen
    errorCode = STACK_ERROR;
    return 0;
  }

  return idxStack[idxTop - off];
}

// Increases the current counter on top of the idxStack 
void idxIncr() {
  if (errorCode != NO_ERROR)
    return;

  if (idxTop <= 0) {
    errorCode = STACK_UNDERFLOW;
    return;
  }

  idxStack[idxTop - 1]++;
}

// Activates the buzzer
void beep()
{
  tone(BUZZER_PIN, BUZZER_SOUND, BUZZER_DURATION);
}

// When text goes beyond 128 symbols the screen scrolls
void scrollScreen()
{
  // Move all symbols one line up in the screen buffer
  for(int i = 16; i < 128; i++)
  {
    screen[i - 16] = screen[i];
  }

  // Fill last line of the screen buffer with space (empty)
  for(int i = 112; i < 128; i++)
  {
    screen[i] = ' ';
  }

  // Displays the screen buffer to the physical screen
  for(int i = 0; i < 128; i++)
  {
    u8x8.drawGlyph(i&0xf, i>>4, screen[i]);
  }
}

// Displays a blinking _ at the current position that we are writing. In this case the time between each blink is 0.5s
void displayCarret()
{
  long int m = millis();
  bool carret = false;

  while (!keyboard.available())
  {
    if(m - millis() >= 500)
    {
      carret = !carret;
      m += 500;
      screenSet(carret ? '_' : ' ');
    }
  }

}

// Displays the remaining buffer(if there is) if we delete all characters on the screen 
// (Not used unless buffer can hold more than 7 lines of text)
void syncToBuffer(int i)
{
  
  int k = (i + 1)>>4;
  k -= min(k, 7);
  
  int j = 0;

  for(k = k * 16; k < i; k++)
  {
    screen[j] = buffer[k];
    u8x8.drawGlyph(j&0xf, j>>4, buffer[k]);
    j++;
  }

  for(; j < 128; j++)
  {
    screen[j] = ' ';
    u8x8.drawGlyph(j&0xf, j>>4, ' ');
  }

  sY = i>>4;
}

// Displays a character at current screen position and updates screen buffer
void screenSet(char c)
{
  u8x8.drawGlyph(sX, sY, c);
  screen[sX + 16 * sY] = c;
}

// Moves the curent position to the beginning of the next line. Scrolls if necessary.
void newLine()
{
  sX = 0;
  sY++;
  if(sY > 7)
  {
    scrollScreen();
    sY = 7;
  }
}

// Gets input from the keyboard
char* getIn(char* buffer, int bufferSize) {
  char c;
  int i = 0;
  

  displayCarret();

  c = keyboard.read();

  // Input cycle. It lasts until the input buffer is returned(When enter is pressed)
  while (true) {
    switch (c) {
      case PS2_ENTER:
        buffer[i] = 0;
        screenSet(' ');
        newLine();
        return buffer;
      case PS2_BACKSPACE:
        if (i == 0) {
          beep(); // Nothing to delete
        } else {
          screenSet(' ');
          sX--;
          i--;
          if(sX < 0)
          {
            sX = 15;
            sY--;
            if(sY < 0)
            {
              sY = 0;
              if(i > 0)
              {
                syncToBuffer(i);
              }
            }
          }
        }
        break;
      case PS2_TAB:
      case PS2_ESC:
      case PS2_PAGEUP:
      case PS2_PAGEDOWN:
      case PS2_UPARROW:
      case PS2_LEFTARROW:
      case PS2_DOWNARROW:
      case PS2_RIGHTARROW:
      case 0:
        beep(); // Keyboard symbols that have no function or are not recognized by the library
        break;

      default:
        if (c < 161) { // Codes 161+ are for special characters
          if (bufferSize - i < 2) {
            beep();
          } else {
            c = toupper(c);
            buffer[i++] = c;
            screenSet(c);
            advance();
            
          }
        }
        break;
    }


    displayCarret();

    c = keyboard.read();
  }

  return buffer;
}

// Advances the cursor one step to the right and if we reach the end displays a new line
void advance()
{
  sX++;
  if(sX > 15)
  {
    newLine();
  }
}

// Prints a string on the display at current cursor position, updating the sceen buffer accordingly
void printStringD(char* str, int n = -1)
{
  while(*str && n != 0)
  {
    screenSet(*str++);
    advance();
    n--;
  }
}


// Fills the buffer using getIn() if necessary and removes any trailing CR and LF from it
// Does nothing if there is still something in the buffer that was not read yet
void input() {
  if (errorCode != NO_ERROR)
    return;

  while (b_idx >= b_end) { // Repeat while we have nothing in the buffer. Can happen multiple times if just ENTER gets pressed
    if (printed) { // New input should start at new line
      newLine();
      printed = false;
    }

    if (getIn(buffer, BUFFER_SIZE) == nullptr) {
      errorCode = INPUT_ERROR;
      return;
    }
    b_idx = 0;
    b_end = strlen(buffer);
    while (b_end > 0 && (buffer[b_end - 1] == '\n' || buffer[b_end - 1] == '\r')) {
      b_end--;
    }
  }
}

// Expects a word from the input buffer and changes indexes according to the word length
int expectWord() {
  int i;

  if (errorCode != NO_ERROR)
    return b_idx;

  // Search for start of word
  for (i = b_idx; i < b_end && (buffer[i] == ' ' || buffer[i] == '\t'); i++)
    ;

  if (b_idx >= b_end) {
    errorCode = EXPECTED_WORD;
    return i;
  }

  b_idx = i;

  // Search for end of word
  for (i = b_idx + 1; i < b_end && buffer[i] != ' ' && buffer[i] != '\t'; i++)
    ;

  return i;
}

// Reads a word from the buffer like expectWord, calling input() to fill it if necessary
int readWord() {
  int i;

  do {
    input(); // Fill the buffer if it is empty

    if (errorCode != NO_ERROR) {
      return b_idx;
    }

    // Search for start of word, slipping whitespace
    for (i = b_idx; i < b_end && (buffer[i] == ' ' || buffer[i] == '\t'); i++)
      ;

  } while (i >= b_end); // Repeat if there is nothing in the buffer

  b_idx = i;

  // Search for end of word, next whitespace or end of the buffer
  for (i = b_idx + 1; i < b_end && buffer[i] != ' ' && buffer[i] != '\t'; i++)
    ;

  return i;
}

// Tells the interpreter if the current word is an integer and if it is we parse it and its value is assigned to the working area of the current word
bool tryParseInt(int idx, word_t* _word) {
  bool hasSign = buffer[b_idx] == '-';
  int i = hasSign ? b_idx + 1 : b_idx;

  if (i >= idx) {
    return false;
  }

  _word->wa.intValue = 0;

  for (; i < idx; i++) {
    if (!isdigit(buffer[i])) {
      return false;
    } else {
      _word->wa.intValue *= 10;
      _word->wa.intValue += buffer[i] - '0';
    }
  }

  if (hasSign) _word->wa.intValue = -_word->wa.intValue;
  _word->type = INTEGER;

  return true;
}

// Detects if a word is a built in and if it can be parsed
bool tryParseBuiltIn(int idx, word_t* _word) {
  if (compare(".", b_idx, idx)) {
    _word->type = DOT;
    return true;
  }
  if (compare("+", b_idx, idx)) {
    _word->type = PLUS;
    return true;
  }
  if (compare("-", b_idx, idx)) {
    _word->type = MINUS;
    return true;
  }
  if (compare("*", b_idx, idx)) {
    _word->type = MULTIPLY;
    return true;
  }
  if (compare("/", b_idx, idx)) {
    _word->type = DIVIDE;
    return true;
  }
  if (compare(".\"", b_idx, idx)) {
    _word->type = PRINT_S;
    return true;
  }
  if (compare(":", b_idx, idx)) {
    _word->type = COLON;
    return true;
  }
  if (compare(";", b_idx, idx)) {
    _word->type = SEMICOLON;
    return true;
  }
  if (compare(".S", b_idx, idx)) {
    _word->type = PRINT_STACK;
    return true;
  }
  if (compare("IF", b_idx, idx)) {
    _word->type = IF;
    return true;
  }
  if (compare("ELSE", b_idx, idx)) {
    _word->type = ELSE;
    return true;
  }
  if (compare("THEN", b_idx, idx)) {
    _word->type = THEN;
    return true;
  }
  if (compare("=", b_idx, idx)) {
    _word->type = EQUAL_TO;
    return true;
  }
  if (compare(">", b_idx, idx)) {
    _word->type = MORE_THAN;
    return true;
  }
  if (compare("<", b_idx, idx)) {
    _word->type = LESS_THAN;
    return true;
  }
  if (compare("<>", b_idx, idx)) {
    _word->type = NOT_EQUAL_TO;
    return true;
  }
  if (compare("0>", b_idx, idx)) {
    _word->type = MORE_THAN_ZERO;
    return true;
  }
  if (compare("0<", b_idx, idx)) {
    _word->type = LESS_THAN_ZERO;
    return true;
  }
  if (compare("0=", b_idx, idx)) {
    _word->type = EQUAL_TO_ZERO;
    return true;
  }
  if (compare("DO", b_idx, idx)) {
    _word->type = DO;
    return true;
  }
  if (compare("LOOP", b_idx, idx)) {
    _word->type = LOOP;
    return true;
  }
  if (compare("I", b_idx, idx)) {
    _word->type = I;
    return true;
  }
  if (compare("CR", b_idx, idx)) {
    _word->type = CR;
    return true;
  }
  if (compare("EMIT", b_idx, idx)) {
    _word->type = EMIT;
    return true;
  }
  if (compare("DUP", b_idx, idx)) {
    _word->type = DUP;
    return true;
  }
  if (compare("MOD", b_idx, idx)) {
    _word->type = MOD;
    return true;
  }
  if (compare("/MOD", b_idx, idx)) {
    _word->type = SLASH_MOD;
    return true;
  }
  if (compare("NEGATE", b_idx, idx)) {
    _word->type = NEGATE;
    return true;
  }
  if (compare("ABS", b_idx, idx)) {
    _word->type = ABS;
    return true;
  }
  if (compare("MAX", b_idx, idx)) {
    _word->type = MAX;
    return true;
  }
  if (compare("MIN", b_idx, idx)) {
    _word->type = MIN;
    return true;
  }
  if (compare("SWAP", b_idx, idx)) {
    _word->type = SWAP;
    return true;
  }
  if (compare("ROT", b_idx, idx)) {
    _word->type = ROT;
    return true;
  }
  if (compare("DROP", b_idx, idx)) {
    _word->type = DROP;
    return true;
  }
  if (compare("NIP", b_idx, idx)) {
    _word->type = NIP;
    return true;
  }
  if (compare("TUCK", b_idx, idx)) {
    _word->type = TUCK;
    return true;
  }
  if (compare("OVER", b_idx, idx)) {
    _word->type = OVER;
    return true;
  }
  if (compare("ROLL", b_idx, idx)) {
    _word->type = ROLL;
    return true;
  }
  if (compare("PICK", b_idx, idx)) {
    _word->type = PICK;
    return true;
  }
  
  return false;
}

// Detects if it is a user defined word and if it can be parsed
bool tryParseDefined(int idx, word_t* _word) {
  definedWordList* wl = wordList;

  while (wl != NULL) {
    if (compare(wl->wordName, b_idx, idx)) {
      _word->type = DEFINED_WORD;
      _word->wa.nodeValue = wl->startNode;
      return true;
    }

    wl = wl->next;
  }

  return false;
}

// Finds an already existing string from the stringList
const char* findExistingString(int idx) {

  for (const string_list* sl = s_list; sl != NULL; sl = sl->getNext()) {
    if (compare(sl->getStrValue(), b_idx, idx)) {
      return sl->getStrValue();
    }
  }

  return NULL;
}

// Adds a string to the stringList
const char* addString(int idx) {
  s_list = new string_list(idx, s_list);
  return s_list->getStrValue();
}

// Seraches for a user defined word 
definedWordList* searchForDefinedWord(const char* wordName) {
  definedWordList* wl = wordList;

  while (wl != NULL) {
    if (strcmp(wl->wordName, wordName) == 0) {
      break;
    }

    wl = wl->next;
  }

  return wl;
}

// Deletes the definition of an user defined word. Used when redefining already defined word
void deleteDefinition(wordNode* startNode) {
  wordNode* node = NULL;

  if (startNode != NULL) {
    node = startNode->next;
  }


  while (startNode != NULL) {
    free(startNode);
    startNode = node;

    if (startNode != NULL) {
      node = startNode->next;
    }
  }
}

// Stage 1 exec - fills all data from the context, reading aditional input or words if neccessary
void execWordStage1(word_t* _word, int& idx) {
  wordNode* startNode = NULL;
  wordNode* node = NULL;
  char* wordName = NULL;

  if (errorCode != NO_ERROR)
    return;

  switch (_word->type) {
    case PRINT_S: // The additional information is the string that follows until " is detected.
      b_idx = idx + 1;
      // Search for "
      for (idx = b_idx; idx < b_end && buffer[idx] != '\"'; idx++)
        ;
      if (idx >= b_end) {
        errorCode = EXPECTED_QUOTE;
        return;
      }

      // Check if that string is already cached in the string list and use the cached version. Or add it to the string list
      _word->wa.strValue = findExistingString(idx);
      if (_word->wa.strValue == NULL) {
        _word->wa.strValue = addString(idx);
      }

      idx++;
      break;
    case COLON: // Word definition logic 
    // Check if we are already in a word
      if (inWordDefinition) {
        errorCode = RECURSIVE_WORD_DEFINITION;
        return;
      }
      inWordDefinition = true;

    // Expect the word that is being defined after the colon.
      b_idx = idx;
      idx = expectWord();
      if (errorCode != NO_ERROR)
        return;
      // We create the name for the word
      wordName = (char*)malloc(idx - b_idx + 1);
      strncpy(wordName, &buffer[b_idx], idx - b_idx + 1);
      wordName[idx - b_idx] = 0; // Puts the '\0' at the end of the name
      // Adds other words following the current word to the wordNode list. Does this until it encounters a semicolon
      do {
        if (node == NULL) {
          node = (wordNode*)malloc(sizeof(wordNode));
          startNode = node;
        } else {
          node->next = (wordNode*)malloc(sizeof(wordNode));
          node = node->next;
        }

        node->next = NULL;

        // Get the next word parse it and execute any stage 1 logic
        b_idx = idx;
        idx = expectWord();
        if (errorCode != NO_ERROR)
          break;
        parseWord(idx, &node->_word);
        if (errorCode != NO_ERROR)
          break;
        execWordStage1(&node->_word, idx);
        if (errorCode != NO_ERROR)
          break;

        // If the word we encounter is LOOP we search for its coresponding DO
        if (node->_word.type == LOOP) {
          wordNode* lastUndefinedDo = NULL;
          wordNode* current = startNode;

          while (current != node) {
            if (current->_word.type == DO && current->_word.wa.nodeValue == NULL) {
              lastUndefinedDo = current;
            }

            current = current->next;
          }

          if (lastUndefinedDo == NULL) {
            errorCode = LONE_LOOP;
            break;
          }

          // Points to the LOOP word
          lastUndefinedDo->_word.wa.nodeValue = node;

          node->_word.wa.nodeValue = lastUndefinedDo;

        } 
        // If the word is THEN we search for its coresponding IF and ELSE words
        else if (node->_word.type == THEN) {
          wordNode* lastUndefinedIf = NULL;
          wordNode* lastUndefinedElse = NULL;
          wordNode* current = startNode;

          while (current != node) {
            if (current->_word.type == ELSE && current->_word.wa.nodeValue == NULL) {
              lastUndefinedElse = current;
            }

            if (current->_word.type == IF && current->_word.wa.nodeValue == NULL) {
              lastUndefinedIf = current;
            }

            current = current->next;
          }

          if (lastUndefinedIf == NULL) {
            errorCode = LONE_THEN;
            break;
          }

          // IF node points to the statements that will be executed if the condition is false.
          // Either the statement following ELSE, or the final THEN if ELSE does not exist
          lastUndefinedIf->_word.wa.nodeValue = lastUndefinedElse != NULL ? lastUndefinedElse->next : node;

          if (lastUndefinedElse != NULL) { // ELSE points to its IF node
            lastUndefinedElse->_word.wa.nodeValue = node;
          }

          node->_word.wa.nodeValue = lastUndefinedIf; //THEN points to its IF node
        }

      } while (node->_word.type != SEMICOLON);

      if (errorCode == NO_ERROR) {
        _word->wa.nodeValue = startNode;

	// Check if the word is already defined
        definedWordList* wl = searchForDefinedWord(wordName);

        if (wl != NULL) { // If it is defined
          deleteDefinition(wl->startNode); // remove the old definition
          free(wordName); // remove redundant word name
        } else { // If it is not defined
       	  // Add it to defined words list
          wl = (definedWordList*)malloc(sizeof(definedWordList));
          wl->next = wordList;
          wl->wordName = wordName;
          wordList = wl;
        }
	// Set the new definition.
        wl->startNode = startNode;
      } else {
        // On error free the allocated memory
        if (wordName != NULL) {
          free(wordName);
        }

        deleteDefinition(startNode);
      }

      inWordDefinition = false;

      break;
    case IF:
    case DO:
      if (!inWordDefinition) {
        errorCode = OUTSIDE_WORD_DEFINITION;
        return;
      }

      _word->wa.nodeValue = NULL;
      defPush(_word->type);

      if (errorCode != NO_ERROR)
        return;

      break;

// Defines logic for adding these words to the defStack so it can be checked if the statement construction is correct
    case ELSE:
    case THEN:
      if (!inWordDefinition) {
        errorCode = OUTSIDE_WORD_DEFINITION;
        return;
      }

      if (defPeek() != IF) {
        if (errorCode != NO_ERROR)
          return;

        errorCode = INVALID_CONSTRUCTION;
        return;
      }

      if (_word->type == THEN) {
        defDiscard();
      }

      _word->wa.nodeValue = NULL;

      break;
    case I:

      if (!inWordDefinition) {
        errorCode = OUTSIDE_WORD_DEFINITION;
        return;
      }

      if (!defHas(DO)) {
        errorCode = INVALID_CONSTRUCTION;
        return;
      }

      _word->wa.nodeValue = NULL;

      break;

    case LOOP:

      if (!inWordDefinition) {
        errorCode = OUTSIDE_WORD_DEFINITION;
        return;
      }

      if (defPeek() != DO) {
        if (errorCode != NO_ERROR)
          return;

        errorCode = INVALID_CONSTRUCTION;
        return;
      }

      defDiscard();

      _word->wa.nodeValue = NULL;

      break;

    default:
      break;
  }
}

// Division funcion 
int divide(int a, int b) {
  if (b == 0) {
    errorCode = DIVISION_BY_ZERO;
    return 0;
  }

  return (int)floor((float)a / b);
}

// Gets the remainder from division
int mod(int a, int b) {
  if (b == 0) {
    errorCode = DIVISION_BY_ZERO;
    return 0;
  }

  return a - divide(a, b) * b;
}

// Gets remainder and divides two values
void slashMod(int a, int b) {
  int d;

  if (b == 0) {
    errorCode = DIVISION_BY_ZERO;
    return;
  }

  d = divide(a, b);
  push(a - d * b);
  push(d);
}

// Implements ROLL word. 
void roll() {
  int idx = pop();

  if (idx < 0) {
    return;
  }

  if (errorCode != NO_ERROR)
    return;

  int val = peek(idx + 1);

  if (errorCode != NO_ERROR)
    return;

  idx = top - idx;

  for (; idx < top; idx++) {
    stack[idx - 1] = stack[idx];
  }

  stack[top - 1] = val;
}

// Implements PICK word.
void pick() {
  int idx = pop();

  if (idx < 0) {
    return;
  }

  if (errorCode != NO_ERROR)
    return;

  int val = peek(idx + 1);

  if (errorCode != NO_ERROR)
    return;

  push(val);
}

// Stage 2 - actual execution of the word
wordNode* execWordStage2(word_t* _word, wordNode* node) {

  int val; 

  if (errorCode != NO_ERROR)
    return NULL;

  // Checks for every word type and executes its logic
  switch (_word->type) {
    case INTEGER:
      push(_word->wa.intValue);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case DOT:
      val = pop();
      if (errorCode != NO_ERROR)
        return NULL;
      sprintf(buffer, "%d ", val);
      printStringD(buffer);
      printed = true;
      break;
    case PLUS:
      push(pop() + pop());
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case MINUS:
      discard(2);
      push(peek(0) - peek(-1));
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case MULTIPLY:
      push(pop() * pop());
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case DIVIDE:
      discard(2);
      if (errorCode != NO_ERROR)
        return NULL;
      val = divide(peek(0), peek(-1));
      if (errorCode != NO_ERROR)
        return NULL;
      push(val);
      break;
    case PRINT_S:
      printStringD(_word->wa.strValue);
      printed = true;
      break;
    case DEFINED_WORD:
      node = _word->wa.nodeValue;
      while (node != NULL) {
        node = execWordStage2(&node->_word, node);
        if (errorCode != NO_ERROR)
          return NULL;

        if (node != NULL) {
          node = node->next;
        }
      }
      break;
    case PRINT_STACK:
      sprintf(buffer, "<%d>: ", top);
      printStringD(buffer);
      for (int i = 0; i < top; i++) {
        sprintf(buffer, "%d ", stack[i]);
        printStringD(buffer);
      }
      printed = true;
      break;
    case EQUAL_TO:
      discard(2);
      push(peek(0) == peek(-1) ? -1 : 0);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case NOT_EQUAL_TO:
      discard(2);
      push(peek(0) != peek(-1) ? -1 : 0);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case MORE_THAN:
      discard(2);
      push(peek(0) > peek(-1) ? -1 : 0);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case LESS_THAN:
      discard(2);
      push(peek(0) < peek(-1) ? -1 : 0);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case MORE_THAN_ZERO:
      push(pop() > 0 ? -1 : 0);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case LESS_THAN_ZERO:
      push(pop() < 0 ? -1 : 0);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case EQUAL_TO_ZERO:
      push(pop() == 0 ? -1 : 0);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case IF:
      // decides which node (after IF or after ELSE) should be executed next
      node = pop() != 0 ? node->next : node->_word.wa.nodeValue;
      if (errorCode != NO_ERROR)
        return NULL;
      // execute all words until THEN (or ELSE) word is reached
      while (node->_word.type != ELSE && node->_word.type != THEN) {
        node = execWordStage2(&node->_word, node);

        if (errorCode != NO_ERROR)
          return NULL;

        if (node == NULL) {
          errorCode = NODE_NULL_IN_IF;
          return NULL;
        }

        node = node->next;
      }

      if (node->_word.type == ELSE) {
        return node->_word.wa.nodeValue;
      } else {
        return node;
      }
      break;
    case CR:
      newLine();
      printed = false;
      break;
    case EMIT:
      val = pop();
      if (errorCode != NO_ERROR)
        return NULL;
      screenSet((char)val);
      advance();
      break;
    case DO:
      discard(2);
      if (errorCode != NO_ERROR)
        return NULL;

      // Push the final value for the loop
      idxPush(peek(0));

      if (errorCode != NO_ERROR)
        return NULL;

      // Push initial counter value for the loop
      idxPush(peek(-1));

      if (errorCode != NO_ERROR)
        return NULL;

      break;

    case LOOP:
      idxIncr(); // increase counter
      if (errorCode != NO_ERROR)
        return NULL;
      val = idxPeek(1);
      if (errorCode != NO_ERROR)
        return NULL;
      val = val == idxPeek(2); // check if the counter reached the final value
      if (errorCode != NO_ERROR)
        return NULL;
      if (val) {
        return node;
      } else {
        return node->_word.wa.nodeValue;
      }
      break;
    case I:
      val = idxPeek(1); // Get current DO..LOOP counter

      if (errorCode != NO_ERROR)
        return NULL;

      push(val);

      if (errorCode != NO_ERROR)
        return NULL;

      break;

    case DUP:
      val = peek(1);

      if (errorCode != NO_ERROR)
        return NULL;

      push(val);

      if (errorCode != NO_ERROR)
        return NULL;

      break;

    case MOD:
      discard(2);
      if (errorCode != NO_ERROR)
        return NULL;
      val = mod(peek(0), peek(-1));
      if (errorCode != NO_ERROR)
        return NULL;
      push(val);
      break;
    case SLASH_MOD:
      discard(2);
      if (errorCode != NO_ERROR)
        return NULL;
      slashMod(peek(0), peek(-1));

      break;

    case NEGATE:
      push(-pop());
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case ABS:
      push(abs(pop()));
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case MAX:
      discard(2);
      if (errorCode != NO_ERROR)
        return NULL;
      val = max(peek(0), peek(-1));
      push(val);
      break;
    case MIN:
      discard(2);
      if (errorCode != NO_ERROR)
        return NULL;
      val = min(peek(0), peek(-1));
      push(val);
      break;
    case SWAP:
      val = peek(2);
      if (errorCode != NO_ERROR)
        return NULL;
      poke(2, peek(1));
      poke(1, val);
      break;
    case ROT:
      val = peek(3);
      if (errorCode != NO_ERROR)
        return NULL;
      poke(3, peek(2));
      poke(2, peek(1));
      poke(1, val);
      break;
    case DROP:
      discard(1);
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case NIP:
      val = pop();
      if (errorCode != NO_ERROR)
        return NULL;
      discard(1);
      if (errorCode != NO_ERROR)
        return NULL;
      push(val);
      break;
    case TUCK:
      val = peek(1);

      if (errorCode != NO_ERROR)
        return NULL;

      push(val);

      if (errorCode != NO_ERROR)
        return NULL;

      poke(2, peek(3));

      if (errorCode != NO_ERROR)
        return NULL;

      poke(3, val);

      break;
    case OVER:
      push(peek(2));
      if (errorCode != NO_ERROR)
        return NULL;
      break;
    case ROLL:
      roll();
      break;
    case PICK:
      pick();
      break;
    default:
      break;
  }

  return node;
}

// Parses a word. First user defined words, then integers, then built in words
void parseWord(int idx, word_t* _word) {
  if (tryParseDefined(idx, _word)) {
    return;
  }
  if (tryParseInt(idx, _word)) {
    return;
  }
  if (tryParseBuiltIn(idx, _word)) {
    return;
  }
  printStringD("??? ");
  printStringD(&buffer[b_idx], idx - b_idx);
  newLine();
  errorCode = UNRECOGNIZED_WORD;
}

// Sets up all needed things and displays a startup message
void setup(void) {
  //Serial.begin(9600);
  keyboard.begin(dataPin, irqPin);
  for(int i = 0; i < SCREEN_SIZE; i++)
  {
    screen[i] = ' ';
  
  }
  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.setPowerSave(0);
  u8x8.clear();
  sY = 3;
  printStringD(welcome);
  delay(4000);
  u8x8.clear();
  sX = 0, sY = 0;

}

// Logic for running the interpreter
void loop() {
  
  int idx;
  word_t _word;


    while(true)
    {
        while(true) // Program loop: read word, execute first stage (compile), then execute second stage(run)
        {
            errorCode = NO_ERROR;

            idx = readWord();

            if(errorCode != NO_ERROR)
                break;
            parseWord(idx, &_word);

            if(errorCode != NO_ERROR)
                break;
            execWordStage1(&_word, idx);

            if(errorCode != NO_ERROR)
                break;
            execWordStage2(&_word, NULL);

            break;
        }

        if(errorCode != NO_ERROR)
        {
            if(printed)
            {
                printed = false;
                newLine();
            }

            printStringD(errorMessages[errorCode]);
            newLine();
            inWordDefinition = false;
            beep();

            if(errorCode == RECURSIVE_WORD_DEFINITION)
            {
                idx = b_end;
            }
        }

        b_idx = idx;


    }

  

}
