/*
 *  Alchematch
 *  by Brett Taylor 2020
 *  Lead development by Daniel King
 *  Original game by Brett Taylor
 *
 *  --------------------
 *  Blinks by Move38
 *  Brought to life via Kickstarter 2018
 *
 *  @madewithblinks
 *  www.move38.com
 *  --------------------
 */

enum blinkStates {INERT, MATCH_MADE, DISSOLVING, BOMB, EXPLODE, R_BOMB, RESOLVE};
byte signalState = INERT;
byte nextState = INERT;
byte specialState = INERT;
bool wasActivated = false;

#define NUM_COLORS 6
byte colorHues[6] = {20,  50,   80,   110,  200,  250};
byte colorSats[6] = {255, 200,  230,  255,  255,  170};
byte blinkColor;
byte previousColor;

Timer dissolveTimer;
#define DISSOLVE_TIME 1500

byte matchesMade = 0;
byte bombActivations = 0;
#define MATCH_GOAL 10
Timer bubbleTimer;
byte bubbleFace;
#define BUBBLE_TIME 300
#define BUBBLE_WAIT_MIN 150
#define BUBBLE_WAIT_MAX 2000

Timer bombClickTimer;
#define BOMB_ACTIVE_TIME 2000

void setup() {
  randomize();
  blinkColor = random(NUM_COLORS - 1);
}

void loop() {

  //run loops
  FOREACH_FACE(f) {
    switch (signalState) {
      case INERT:
        inertLoop();
        break;
      case MATCH_MADE:
        matchmadeLoop();
        break;
      case DISSOLVING:
        dissolvingLoop();
        break;
      case EXPLODE:
        explodeLoop();
        break;
      case BOMB:
        bombLoop();
        break;
      case RESOLVE:
        resolveLoop();
        break;
    }
  }

  //send data
  byte sendData = (signalState << 3) + (blinkColor);
  setValueSentOnAllFaces(sendData);

  //temp display
  switch (signalState) {
    case INERT:
    case MATCH_MADE:
      inertDisplay();
      break;
    case BOMB:
    case EXPLODE:
      setColor(OFF);
      setColorOnFace(makeColorHSB(colorHues[blinkColor], colorSats[blinkColor], 255), (millis() / 100) % 6);
      break;
    case DISSOLVING:
      dissolveDisplay();
      break;
  }

  //dump button presses
  buttonMultiClicked();
  buttonPressed();
}

////DISPLAY FUNCTIONS

#define SWIRL_INTERVAL 50

void dissolveDisplay() {
  if (dissolveTimer.getRemaining() > DISSOLVE_TIME / 2) {//first half

    byte dissolveBrightness = map(dissolveTimer.getRemaining() - (DISSOLVE_TIME / 2), 0, DISSOLVE_TIME / 2, 0, 255);
    setColor(makeColorHSB(colorHues[previousColor], 255, dissolveBrightness));

  } else {//second half

    byte dissolveBrightness = map(dissolveTimer.getRemaining(), 0, DISSOLVE_TIME / 2, 0, 255);

    if (nextState == BOMB) {
      setColor(OFF);
      setColorOnFace(makeColorHSB(colorHues[blinkColor], colorSats[blinkColor], 255 - dissolveBrightness), (millis() / 100) % 6);
    } else {
      setColor(makeColorHSB(colorHues[blinkColor], colorSats[blinkColor], 255 - dissolveBrightness));
    }

  }

  //also do the little swirly goo
  byte swirlFrame = (DISSOLVE_TIME - dissolveTimer.getRemaining()) / SWIRL_INTERVAL;
  if (swirlFrame < 8) {
    FOREACH_FACE(f) {
      if (f == swirlFrame % 6) {
        setColorOnFace(WHITE, f);
      }
    }
  }

}

void inertDisplay() {

  setColor(makeColorHSB(colorHues[blinkColor], colorSats[blinkColor], 255));

  //do bubbles if it's bubble time
  if (specialState == INERT) {
    if (bubbleTimer.isExpired()) {//the timer is over, just reset
      //what's our current wait time?
      bubbleTimer.set(map(MATCH_GOAL - matchesMade, 0, MATCH_GOAL, BUBBLE_WAIT_MIN, BUBBLE_WAIT_MAX));
      bubbleFace = (bubbleFace + random(4) + 1) % 6;//choose a new bubble face
    } else if (bubbleTimer.getRemaining() <= BUBBLE_TIME) {//ooh, we are actively bubbling right now
      //how bubbly are we right now?
      byte bubbleSat = map(BUBBLE_TIME - bubbleTimer.getRemaining(), 0, BUBBLE_TIME, (colorSats[blinkColor] - 155), colorSats[blinkColor]);
      setColorOnFace(makeColorHSB(colorHues[blinkColor], bubbleSat, 255), bubbleFace);
    }
  }

}

void inertLoop() {
  //search my neighbors, see if I can make a match
  byte sameColorNeighbors = 0;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      byte neighborData = getLastValueReceivedOnFace(f);
      //check to see if it's INERT
      if (getNeighborColor(neighborData) == blinkColor) {//ok, this is our color
        if (getNeighborState(neighborData) == INERT) {//this one is inert, but could still help the matches
          sameColorNeighbors++;
        } else if (getNeighborState(neighborData) == MATCH_MADE) {//this on is in matchmade!
          sameColorNeighbors += 2;//just automatically get us high enough
        }
      }
    }
  }

  if (sameColorNeighbors >= 2) {
    signalState = MATCH_MADE;
  }

  listenForExplode();

  if (buttonMultiClicked()) {
    if (buttonClickCount() == 3) {
      signalState = DISSOLVING;
      createNewBlink();
      dissolveTimer.set(DISSOLVE_TIME);
    }
  }
}

void bombLoop() {

  //listen for button clicks
  if (buttonPressed()) {
    signalState = EXPLODE;
    bombActivations++;
  }

  if (bombClickTimer.isExpired()) {
    //so here we need to revert to a regular dude
    matchesMade = 0;
    nextState = INERT;
    specialState = INERT;
    signalState = INERT;
  }

  //search my neighbors to see if there are special things (EXPLODE or BUCKET)
  listenForExplode();

}

void matchmadeLoop() {
  //so in here, we listen to make sure no same-color neighbor is waiting
  bool foundUnmatchedNeighbors = false;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      byte neighborData = getLastValueReceivedOnFace(f);
      if (getNeighborState(neighborData) == INERT && getNeighborColor(neighborData) == blinkColor) {
        //this neighbor is my color and inert - we need to wait to dissolve
        foundUnmatchedNeighbors = true;
      }
    }
  }

  if (foundUnmatchedNeighbors == false) {
    signalState = DISSOLVING;
    createNewBlink();
    dissolveTimer.set(DISSOLVE_TIME);
  }

  listenForExplode();
}

void dissolvingLoop() {
  if (dissolveTimer.isExpired()) {
    signalState = nextState;
    bombClickTimer.set(BOMB_ACTIVE_TIME);
  }

  listenForExplode();
}

void listenForExplode() {
  //search my neighbors to see if there are special things (EXPLODE or BUCKET)
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      byte neighborData = getLastValueReceivedOnFace(f);
      if (getNeighborState(neighborData) == EXPLODE) {
        //so this makes us into a matching color thing
        signalState = MATCH_MADE;
        blinkColor = getNeighborColor(neighborData);
        bombActivations++;
      }
    }
  }
}

void resolveLoop() {

}

void explodeLoop() {
  //so in here all we do is make sure all of our neighbors are in MATCH_MADE
  bool hasInertNeighbors = false;

  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      byte neighborData = getLastValueReceivedOnFace(f);
      if (getNeighborState(neighborData) == INERT) {
        hasInertNeighbors = true;
      }
    }
  }

  if (hasInertNeighbors == false) {
    signalState = MATCH_MADE;
  }
}

void bucketLoop() {

}

void createNewBlink() {

  //always become a new color
  previousColor = blinkColor;
  blinkColor = (blinkColor + random(NUM_COLORS - 2) + 1) % NUM_COLORS;

  if (specialState == INERT) {//this is a regular blink becoming a new blink

    matchesMade++;
    if (matchesMade >= MATCH_GOAL) {//normal blink, may upgrade

      nextState = BOMB;
      specialState = EXPLODE;

    } else {
      nextState = INERT;
      specialState = INERT;
    }

  } else {//special blinks simply reset to 0

    matchesMade = 0;
    nextState = INERT;
    specialState = INERT;

  }

}

byte getNeighborState(byte data) {
  return ((data >> 3) & 7);
}

byte getNeighborColor(byte data) {
  return (data & 7);
}
