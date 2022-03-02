// test new text
// Required libs
#include "FastLED.h"
#include "I2Cdev.h"
#include "MPU6050.h"
#include "Wire.h"
#include "iSin.h"
#include "RunningMedian.h"

//SFX Libs
#include <Metro.h>
#include <AltSoftSerial.h>
#include <wavTrigger.h>

// Included libs
#include "Enemy.h"
#include "Particle.h"
#include "Spawner.h"
#include "Lava.h"
#include "Boss.h"
#include "Conveyor.h"

MPU6050 accelgyroIC2(0x69);
MPU6050 accelgyroIC1(0x68);

int16_t ax1, ay1, az1;
int16_t gx1, gy1, gz1;

int16_t ax2, ay2, az2;
int16_t gx2, gy2, gz2;

// LED setup
#define NUM_LEDS 300
#define DATA_PIN 3
#define LED_COLOR_ORDER BGR //this is right for my string, but could be others (GBR)
#define BRIGHTNESS 150
#define DIRECTION 0            // 0 = right to left, 1 = left to right
#define MIN_REDRAW_INTERVAL 16 // Min redraw interval (ms) 33 = 30fps / 16 = 63fps
#define USE_GRAVITY 0          // 0/1 use gravity (LED strip going up wall)
#define BEND_POINT 550         // 0/1000 point at which the LED strip goes up the wall

// SFX Variables
Metro gLedMetro(500);  // LED blink interval timer
Metro gSeqMetro(6000); // Sequencer state machine interval timer
wavTrigger wTrig;

byte gLedState = 0;  // LED State
int gSeqState = 0;   // Main program sequencer state
int gRateOffset = 0; // WAV Trigger sample-rate offset

// GAME
long previousMillis = 0; // Time of the last redraw
int levelNumber = 0;
long lastInputTime = 0;
long lastInputTime2 = 0;

#define TIMEOUT 30000
#define LEVEL_COUNT 9
#define MAX_VOLUME 10
iSin isin = iSin();

// JOYSTICK
#define JOYSTICK_ORIENTATION 1 // 0, 1 or 2 to set the angle of the joystick
#define JOYSTICK_DIRECTION 1   // 0/1 to flip joystick direction
#define ATTACK_THRESHOLD 30000 // The threshold that triggers an attack
#define JOYSTICK_DEADZONE 5    // Angle to ignore
int joystickTilt = 0;          // Stores the angle of the joystick
int joystickWobble = 0;        // Stores the max amount of acceleration (wobble)
int joystickTilt2 = 0;         // Stores the angle of the joystick
int joystickWobble2 = 0;

// WOBBLE ATTACK
#define ATTACK_WIDTH 70     // Width of the wobble attack, world is 1000 wide
#define ATTACK_DURATION 500 // Duration of a wobble attack (ms)
long attackMillis = 0;      // Time the attack started
bool attacking = 0;
long attackMillis2 = 0;
bool attacking2 = 0; // Is the attack in progress?
#define BOSS_WIDTH 40

// PLAYER
#define MAX_PLAYER_SPEED 8  // Max move speed of the player
char *stage;                // what stage the game is at (PLAY/DEAD/WIN/GAMEOVER)
long stageStartTime;        // Stores the time the stage changed for stages that are time based
int playerPosition;         // Stores the player position
int playerPositionModifier; // +/- adjustment to player position
int playerPosition2;        // Stores the player position
int playerPositionModifier2;
bool playerAlive;
long killTime;
int lives = 3;

// POOLS
int lifeLEDs[3] = {52, 50, 40};
Enemy enemyPool[10] = {
    Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy(), Enemy()};
int const enemyCount = 10;
Particle particlePool[40] = {
    Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle(), Particle()};
int const particleCount = 40;
Spawner spawnPool[2] = {
    Spawner(), Spawner()};
int const spawnCount = 2;
Lava lavaPool[4] = {
    Lava(), Lava(), Lava(), Lava()};
int const lavaCount = 4;
Conveyor conveyorPool[2] = {
    Conveyor(), Conveyor()};
int const conveyorCount = 2;
Boss boss = Boss();

CRGB leds[NUM_LEDS];
RunningMedian MPUAngleSamples = RunningMedian(5);
RunningMedian MPUWobbleSamples = RunningMedian(5);
RunningMedian MPUAngleSamples2 = RunningMedian(5);
RunningMedian MPUWobbleSamples2 = RunningMedian(5);

void setup()
{
    Serial.begin(9600);
    while (!Serial)
        ;

    // MPU
    Wire.begin();
    TWBR = 6; // set 400kHz mode @ 16MHz CPU or 200kHz mode @ 8MHz CPU (Sam added)
    accelgyroIC1.initialize();
    accelgyroIC2.initialize();

    // SFX Starters
    wTrig.start();
    delay(10);
    wTrig.stopAllTracks();
    wTrig.samplerateOffset(0);
    wTrig.trackGain(1, 20);
    wTrig.trackPlayPoly(1);

    // Fast LED
    //    FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, LED_COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.addLeds<WS2812B, DATA_PIN, LED_COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    FastLED.setDither(1);

    // Life LEDs
    for (int i = 0; i < 3; i++)
    {
        pinMode(lifeLEDs[i], OUTPUT);
        digitalWrite(lifeLEDs[i], HIGH);
    }

    loadLevel();
}

////////// LOOP /////////////
////////////////////////////

void loop()
{
    long mm = millis();
    int brightness = 0;

    if (stage == "PLAY")
    {
        if (attacking || attacking2)
        {
            // SFXattacking(); // Attack Sound
        }
        else
        {
            SFXtilt(joystickTilt); // Tilt Sound
        }
    }
    else if (stage == "DEAD")
    {
        // SFXdead(); // Death Sound
    }

    if (mm - previousMillis >= MIN_REDRAW_INTERVAL)
    {
        getInput();
        getInput2();
        long frameTimer = mm;
        previousMillis = mm;

        if (abs(joystickTilt) > JOYSTICK_DEADZONE)
        {
            lastInputTime = mm;
            if (stage == "SCREENSAVER")
            {
                levelNumber = -1;
                stageStartTime = mm;
                stage = "WIN";
            }
        }
        else
        {
            if (lastInputTime + TIMEOUT && lastInputTime + TIMEOUT < mm)
            {
                stage = "SCREENSAVER";
            }
        }
        if (stage == "SCREENSAVER")
        {
            screenSaverTick();
        }
        else if (stage == "PLAY")
        {
            // PLAYING
            if (attacking && attackMillis + ATTACK_DURATION < mm)
                attacking = 0;

            if (attacking2 && attackMillis2 + ATTACK_DURATION < mm)
                attacking2 = 0;

            // If player not attacking, check if they should be
            if (!attacking && joystickWobble > ATTACK_THRESHOLD)
            {
                SFXattacking();
                attackMillis = mm;
                attacking = 1;
            }

            if (!attacking2 && joystickWobble2 > ATTACK_THRESHOLD)
            {
                SFXattacking();
                attackMillis2 = mm;
                attacking2 = 1;
            }

            // If still not attacking, move!
            playerPosition += playerPositionModifier;

            if (!attacking)
            {
                int moveAmount = (joystickTilt / 6.0);

                if (DIRECTION)
                    moveAmount = -moveAmount;

                moveAmount = constrain(moveAmount, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
                playerPosition -= moveAmount;

                if (playerPosition < 0)
                    playerPosition = 0;
                if (playerPosition > 1000)
                    playerPosition = 1000;

                if (abs(playerPosition - playerPosition2) <= 3 && !boss.Alive())
                {
                    // Reached exit!
                    levelComplete();
                    return;
                }
            }

            ////// PLAYER 2 MOVE

            playerPosition2 += playerPositionModifier2;

            if (!attacking2)
            {
                int moveAmount2 = (joystickTilt2 / 6.0);

                if (DIRECTION)
                    moveAmount2 = -moveAmount2;

                moveAmount2 = constrain(moveAmount2, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
                playerPosition2 -= moveAmount2;

                if (playerPosition2 < 0)
                    playerPosition2 = 0;
                if (playerPosition2 > 1000)
                    playerPosition2 = 1000;
            }

            if (inLava(playerPosition))
            {
                SFXdead();
                die();
            }

            if (inLava(playerPosition2))
            {
                SFXdead();
                die2();
            }

            // Ticks and draw calls
            FastLED.clear();
            tickConveyors();
            tickSpawners();
            tickBoss();
            tickLava();
            tickEnemies();
            drawPlayer();
            drawAttack();
            drawAttack2();
            // drawExit();
        }
        else if (stage == "DEAD")
        {
            // DEAD
            FastLED.clear();
            if (!tickParticles())
            {
                loadLevel();
            }
        }
        else if (stage == "WIN")
        {
            // SFXwin();
            // LEVEL COMPLETE
            // SFXwin(); // Win sound
            FastLED.clear();
            if (stageStartTime + 500 > mm)
            {
                int n = max(map(((mm - stageStartTime)), 0, 500, playerPosition, 0), 0);
                for (int i = NUM_LEDS; i >= n; i--)
                {
                    brightness = 255;
                    leds[i] = CRGB(0, brightness, 0);
                }

                // SFXwin(); // Win sound
            }
            else if (stageStartTime + 1000 > mm)
            {
                int n = max(map(((mm - stageStartTime)), 500, 1000, NUM_LEDS, 0), 0);
                for (int i = 0; i < n; i++)
                {
                    brightness = 255;
                    leds[i] = CRGB(0, brightness, 0);
                }
                // SFXwin(); // Wind sound
            }
            else if (stageStartTime + 1200 > mm)
            {
                leds[0] = CRGB(0, 255, 0);
            }
            else
            {
                nextLevel();
                // SFXwin();
            }
        }
        else if (stage == "COMPLETE")
        {
            FastLED.clear();
            if (stageStartTime + 500 > mm)
            {
                int n = max(map(((mm - stageStartTime)), 0, 500, NUM_LEDS, 0), 0);
                for (int i = NUM_LEDS; i >= n; i--)
                {
                    brightness = (sin(((i * 10) + mm) / 500.0) + 1) * 255;
                    leds[i].setHSV(brightness, 255, 50);
                }
            }
            else if (stageStartTime + 5000 > mm)
            {
                for (int i = NUM_LEDS; i >= 0; i--)
                {
                    brightness = (sin(((i * 10) + mm) / 500.0) + 1) * 255;
                    leds[i].setHSV(brightness, 255, 50);
                }
            }
            else if (stageStartTime + 5500 > mm)
            {
                int n = max(map(((mm - stageStartTime)), 5000, 5500, NUM_LEDS, 0), 0);
                for (int i = 0; i < n; i++)
                {
                    brightness = (sin(((i * 10) + mm) / 500.0) + 1) * 255;
                    leds[i].setHSV(brightness, 255, 50);
                }
            }
            else
            {
                nextLevel();
            }
        }
        else if (stage == "GAMEOVER")
        {
            // GAME OVER!
            FastLED.clear();
            stageStartTime = 0;
        }

        Serial.print(millis() - mm);
        Serial.print(" - ");
        FastLED.show();
        Serial.println(millis() - mm);
    }
}

// ---------------------------------
// ------------ LEVELS -------------
// ---------------------------------

void loadLevel()
{
    updateLives();
    cleanupLevel();
    playerPosition = 0;
    playerAlive = 1;
    switch (levelNumber)
    {
    case 0:
        // Left or right?
        // wTrig.stopAllTracks();
        // wTrig.trackPlayPoly(1);
        playerPosition = 200;
        playerPosition2 = 800;
        spawnEnemy(1, 0, 0, 0); // commenting out for testing
        spawnEnemy(1000, 0, 0, 0);
        break;
    case 1:
        // Slow moving enemy
        // wTrig.stopAllTracks();
        // wTrig.trackPlayPoly(1);
        playerPosition = 0;
        playerPosition2 = 1000;
        spawnEnemy(500, 0, 1, 0);
        spawnEnemy(500, 0, -1, 0);
        break;
    case 2:
        // Spawning enemies at exit every 4 seconds
        playerPosition = 0;
        playerPosition2 = 1000;
        spawnPool[0].Spawn(500, 4000, 2, 0, 0);
        spawnPool[1].Spawn(500, 4000, 2, 1, 0);

        if (playerPosition > 200)
            spawnPool[2].Spawn(0, 4000, 2, 1, 1);

        break;
    case 3:
        // Lava intro
        playerPosition = 0;
        playerPosition2 = 1000;
        spawnLava(430, 510, 2000, 2000, 660, "OFF");
        spawnLava(250, 340, 2000, 2000, 1320, "OFF");
        spawnLava(600, 690, 2000, 2000, 0, "OFF");
        break;
    case 4:
        // Sin enemy
        playerPosition = 0;
        playerPosition2 = 1000;
        spawnEnemy(400, 1, 5, 275);
        spawnEnemy(500, 1, 7, 250);
        spawnEnemy(600, 1, 3, 250);
        break;
    case 5:
        // Conveyor
        playerPosition = 0;
        playerPosition2 = 1000;
        spawnConveyor(300, 550, -1);
        spawnConveyor(551, 700, 1);
        spawnEnemy(810, 0, 0, 0);
        spawnEnemy(250, 0, 0, 0);
        break;
    case 6:
        // Conveyor of enemies
        playerPosition = 0;
        playerPosition2 = 1000;
        spawnConveyor(50, 490, 1);
        spawnConveyor(510, 950, -1);
        spawnEnemy(150, 0, 0, 0);
        spawnEnemy(350, 0, 0, 0);
        spawnEnemy(500, 0, 0, 0);
        spawnEnemy(850, 0, 0, 0);
        spawnEnemy(650, 0, 0, 0);
        break;
    case 7:
        // Lava run
        playerPosition = 0;
        playerPosition2 = 1000;
        spawnLava(195, 300, 2000, 2000, 0, "OFF");
        spawnLava(350, 455, 2000, 2000, 0, "OFF");
        spawnLava(510, 610, 2000, 2000, 0, "OFF");
        spawnLava(660, 760, 2000, 2000, 0, "OFF");
        spawnPool[0].Spawn(500, 3800, 4, 0, 0);
        break;
    case 8:
        // Sin enemy #2
        spawnEnemy(700, 1, 7, 275);
        spawnEnemy(500, 1, 5, 250);
        spawnPool[0].Spawn(1000, 5500, 4, 0, 3000);
        spawnPool[1].Spawn(0, 5500, 5, 1, 10000);
        spawnConveyor(100, 900, -1);
        break;
    case 9:
        // Boss
        spawnBoss();
        break;
    }
    stageStartTime = millis();
    stage = "PLAY";
}

void spawnBoss()
{
    boss.Spawn();
    moveBoss();
}

void moveBoss()
{
    int spawnSpeed = 2500;
    if (boss._lives == 2)
        spawnSpeed = 2000;
    if (boss._lives == 1)
        spawnSpeed = 1500;
    spawnPool[0].Spawn(boss._pos, spawnSpeed, 3, 0, 0);
    spawnPool[1].Spawn(boss._pos, spawnSpeed, 3, 1, 0);
}

void spawnEnemy(int pos, int dir, int sp, int wobble)
{
    for (int e = 0; e < enemyCount; e++)
    {
        if (!enemyPool[e].Alive())
        {
            enemyPool[e].Spawn(pos, dir, sp, wobble);
            enemyPool[e].playerSide = pos > playerPosition ? 1 : -1;
            enemyPool[e].playerSide2 = pos > playerPosition2 ? 1 : -1;
            return;
        }
    }
}

void spawnLava(int left, int right, int ontime, int offtime, int offset, char *state)
{
    for (int i = 0; i < lavaCount; i++)
    {
        if (!lavaPool[i].Alive())
        {
            lavaPool[i].Spawn(left, right, ontime, offtime, offset, state);
            return;
        }
    }
}

void spawnConveyor(int startPoint, int endPoint, int dir)
{
    for (int i = 0; i < conveyorCount; i++)
    {
        if (!conveyorPool[i]._alive)
        {
            conveyorPool[i].Spawn(startPoint, endPoint, dir);
            return;
        }
    }
}

void cleanupLevel()
{
    for (int i = 0; i < enemyCount; i++)
    {
        enemyPool[i].Kill();
    }
    for (int i = 0; i < particleCount; i++)
    {
        particlePool[i].Kill();
    }
    for (int i = 0; i < spawnCount; i++)
    {
        spawnPool[i].Kill();
    }
    for (int i = 0; i < lavaCount; i++)
    {
        lavaPool[i].Kill();
    }
    for (int i = 0; i < conveyorCount; i++)
    {
        conveyorPool[i].Kill();
    }
    boss.Kill();
}

void levelComplete()
{
    stageStartTime = millis();
    SFXwin();
    stage = "WIN";
    if (levelNumber == LEVEL_COUNT)
    {
        SFXcomplete();
        stage = "COMPLETE";
    }
    lives = 3;
    updateLives();
}

void nextLevel()
{
    levelNumber++;
    if (levelNumber > LEVEL_COUNT)
        levelNumber = 0;
    loadLevel();
}

void gameOver()
{
    levelNumber = 0;
    SFXGameOver();
    loadLevel();
}

void die()
{
    playerAlive = 0;
    if (levelNumber > 0)
        lives--;
    updateLives();
    if (lives == 0)
    {
        levelNumber = 0;
        lives = 3;
    }
    for (int p = 0; p < particleCount; p++)
    {
        particlePool[p].Spawn(playerPosition);
    }
    stageStartTime = millis();
    stage = "DEAD";
    killTime = millis();
}

void die2()
{
    playerAlive = 0;
    if (levelNumber > 0)
        lives--;
    updateLives();
    if (lives == 0)
    {
        levelNumber = 0;
        lives = 3;
    }
    for (int p = 0; p < particleCount; p++)
    {
        particlePool[p].Spawn(playerPosition2);
    }
    stageStartTime = millis();
    stage = "DEAD";
    killTime = millis();
}

// ----------------------------------
// -------- TICKS & RENDERS ---------
// ----------------------------------
void tickEnemies()
{
    for (int i = 0; i < enemyCount; i++)
    {
        if (enemyPool[i].Alive())
        {
            enemyPool[i].Tick();
            // Hit attack?
            if (attacking)
            {
                if (enemyPool[i]._pos > playerPosition - (ATTACK_WIDTH / 2) && enemyPool[i]._pos < playerPosition + (ATTACK_WIDTH / 2))
                {
                    enemyPool[i].Kill();
                    SFXkill(); // Kill sound
                }
            }

            if (attacking2)
            {
                if (enemyPool[i]._pos > playerPosition2 - (ATTACK_WIDTH / 2) && enemyPool[i]._pos < playerPosition2 + (ATTACK_WIDTH / 2))
                {
                    enemyPool[i].Kill();
                    SFXkill(); // Kill sound
                }
            }

            if (inLava(enemyPool[i]._pos))
            {
                enemyPool[i].Kill();
                SFXkill(); // Kill sound
            }
            // Draw (if still alive)
            if (enemyPool[i].Alive())
            {
                leds[getLED(enemyPool[i]._pos)] = CRGB(255, 0, 0);
            }
            // Hit player?
            if (
                (enemyPool[i].playerSide == 1 && enemyPool[i]._pos <= playerPosition) ||
                (enemyPool[i].playerSide == -1 && enemyPool[i]._pos >= playerPosition))
            {
                SFXdead();
                die();
                return;
            }

            if (
                (enemyPool[i].playerSide2 == 1 && enemyPool[i]._pos <= playerPosition2) ||
                (enemyPool[i].playerSide2 == -1 && enemyPool[i]._pos >= playerPosition2))
            {
                SFXdead();
                die2();
                return;
            }
        }
    }
}

void tickBoss()
{
    // DRAW
    if (boss.Alive())
    {
        boss._ticks++;
        for (int i = getLED(boss._pos - BOSS_WIDTH / 2); i <= getLED(boss._pos + BOSS_WIDTH / 2); i++)
        {
            leds[i] = CRGB::DarkRed;
            leds[i] %= 100;
        }
        // CHECK COLLISION
        if (getLED(playerPosition) > getLED(boss._pos - BOSS_WIDTH / 2) && getLED(playerPosition) < getLED(boss._pos + BOSS_WIDTH))
        {
            SFXdead();
            die();
            return;
        }
        // CHECK FOR ATTACK
        if (attacking)
        {
            if (
                (getLED(playerPosition + (ATTACK_WIDTH / 2)) >= getLED(boss._pos - BOSS_WIDTH / 2) && getLED(playerPosition + (ATTACK_WIDTH / 2)) <= getLED(boss._pos + BOSS_WIDTH / 2)) ||
                (getLED(playerPosition - (ATTACK_WIDTH / 2)) <= getLED(boss._pos + BOSS_WIDTH / 2) && getLED(playerPosition - (ATTACK_WIDTH / 2)) >= getLED(boss._pos - BOSS_WIDTH / 2)))
            {
                boss.Hit();
                if (boss.Alive())
                {
                    moveBoss();
                }
                else
                {
                    spawnPool[0].Kill();
                    spawnPool[1].Kill();
                }
            }
        }
    }
}

void drawPlayer()
{
    leds[getLED(playerPosition)] = CRGB(0, 255, 0);
    leds[getLED(playerPosition2)] = CRGB(0, 0, 255);
}

// get rid of later
// void drawExit()
// {
//     if (!boss.Alive())
//     {
//         leds[NUM_LEDS - 1] = CRGB(0, 0, 255);
//     }
// }

void tickSpawners()
{
    long mm = millis();
    for (int s = 0; s < spawnCount; s++)
    {
        if (spawnPool[s].Alive() && spawnPool[s]._activate < mm)
        {
            if (spawnPool[s]._lastSpawned + spawnPool[s]._rate < mm || spawnPool[s]._lastSpawned == 0)
            {
                spawnEnemy(spawnPool[s]._pos, spawnPool[s]._dir, spawnPool[s]._sp, 0);
                spawnPool[s]._lastSpawned = mm;
            }
        }
    }
}

void tickLava()
{
    int A, B, p, i, brightness, flicker;
    long mm = millis();
    Lava LP;
    for (i = 0; i < lavaCount; i++)
    {
        flicker = random8(5);
        LP = lavaPool[i];
        if (LP.Alive())
        {
            A = getLED(LP._left);
            B = getLED(LP._right);
            if (LP._state == "OFF")
            {
                if (LP._lastOn + LP._offtime < mm)
                {
                    LP._state = "ON";
                    LP._lastOn = mm;
                }
                for (p = A; p <= B; p++)
                {
                    leds[p] = CRGB(3 + flicker, (3 + flicker) / 1.5, 0);
                }
            }
            else if (LP._state == "ON")
            {
                if (LP._lastOn + LP._ontime < mm)
                {
                    LP._state = "OFF";
                    LP._lastOn = mm;
                }
                for (p = A; p <= B; p++)
                {
                    leds[p] = CRGB(150 + flicker, 100 + flicker, 0);
                }
            }
        }
        lavaPool[i] = LP;
    }
}

bool tickParticles()
{
    bool stillActive = false;
    for (int p = 0; p < particleCount; p++)
    {
        if (particlePool[p].Alive())
        {
            particlePool[p].Tick(USE_GRAVITY);
            leds[getLED(particlePool[p]._pos)] += CRGB(particlePool[p]._power, 0, 0);
            stillActive = true;
        }
    }
    return stillActive;
}

void tickConveyors()
{
    int b, dir, n, i, ss, ee, led;
    long m = 10000 + millis();
    playerPositionModifier = 0;
    playerPositionModifier2 = 0;

    for (i = 0; i < conveyorCount; i++)
    {
        if (conveyorPool[i]._alive)
        {
            dir = conveyorPool[i]._dir;
            ss = getLED(conveyorPool[i]._startPoint);
            ee = getLED(conveyorPool[i]._endPoint);
            for (led = ss; led < ee; led++)
            {
                b = 5;
                n = (-led + (m / 100)) % 5;
                if (dir == -1)
                    n = (led + (m / 100)) % 5;
                b = (5 - n) / 2.0;
                if (b > 0)
                    leds[led] = CRGB(0, 0, b);
            }

            if (playerPosition > conveyorPool[i]._startPoint && playerPosition < conveyorPool[i]._endPoint)
            {
                if (dir == -1)
                {
                    playerPositionModifier = -(MAX_PLAYER_SPEED - 4);
                }
                else
                {
                    playerPositionModifier = (MAX_PLAYER_SPEED - 4);
                }
            }

            if (playerPosition2 > conveyorPool[i]._startPoint && playerPosition2 < conveyorPool[i]._endPoint)
            {
                if (dir == -1)
                {
                    playerPositionModifier2 = -(MAX_PLAYER_SPEED - 4);
                }
                else
                {
                    playerPositionModifier2 = (MAX_PLAYER_SPEED - 4);
                }
            }
        }
    }
}

void drawAttack()
{
    if (!attacking)
        return;

    int n = map(millis() - attackMillis, 0, ATTACK_DURATION, 100, 5);
    for (int i = getLED(playerPosition - (ATTACK_WIDTH / 2)) + 1; i <= getLED(playerPosition + (ATTACK_WIDTH / 2)) - 1; i++)
    {
        leds[i] = CRGB(0, 0, n);
    }
    if (n > 90)
    {
        n = 255;
        leds[getLED(playerPosition)] = CRGB(255, 255, 255);
    }
    else
    {
        n = 0;
        leds[getLED(playerPosition)] = CRGB(0, 255, 0);
    }
    leds[getLED(playerPosition - (ATTACK_WIDTH / 2))] = CRGB(n, n, 255);
    leds[getLED(playerPosition + (ATTACK_WIDTH / 2))] = CRGB(n, n, 255);
}

int getLED(int pos)
{
    // The world is 1000 pixels wide, this converts world units into an LED number
    return constrain((int)map(pos, 0, 1000, 0, NUM_LEDS - 1), 0, NUM_LEDS - 1);
}

/// PLAYER2 DRAW ATTACK

void drawAttack2()
{
    if (!attacking2)
        return;
    int n = map(millis() - attackMillis2, 0, ATTACK_DURATION, 100, 5);
    for (int i = getLED(playerPosition2 - (ATTACK_WIDTH / 2)) + 1; i <= getLED(playerPosition2 + (ATTACK_WIDTH / 2)) - 1; i++)
    {
        leds[i] = CRGB(0, 0, n);
    }
    if (n > 90)
    {
        n = 255;
        leds[getLED(playerPosition2)] = CRGB(255, 255, 255);
    }
    else
    {
        n = 0;
        leds[getLED(playerPosition2)] = CRGB(0, 255, 0);
    }
    leds[getLED(playerPosition2 - (ATTACK_WIDTH / 2))] = CRGB(n, n, 255);
    leds[getLED(playerPosition2 + (ATTACK_WIDTH / 2))] = CRGB(n, n, 255);
}

bool inLava(int pos)
{
    // Returns if the player is in active lava
    int i;
    Lava LP;
    for (i = 0; i < lavaCount; i++)
    {
        LP = lavaPool[i];
        if (LP.Alive() && LP._state == "ON")
        {
            if (LP._left < pos && LP._right > pos)
                return true;
        }
    }
    return false;
}

void updateLives()
{
    // Updates the life LEDs to show how many lives the player has left
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(lifeLEDs[i], lives > i ? HIGH : LOW);
    }
}

// ---------------------------------
// --------- SCREENSAVER -----------
// ---------------------------------
void screenSaverTick()
{
    int n, b, c, i;
    long mm = millis();
    int mode = (mm / 20000) % 2;

    for (i = 0; i < NUM_LEDS; i++)
    {
        leds[i].nscale8(250);
    }
    if (mode == 0)
    {
        // Marching green <> orange
        n = (mm / 250) % 10;
        b = 10 + ((sin(mm / 500.00) + 1) * 20.00);
        c = 20 + ((sin(mm / 5000.00) + 1) * 33);
        for (i = 0; i < NUM_LEDS; i++)
        {
            if (i % 10 == n)
            {
                leds[i] = CHSV(c, 255, 150);
            }
        }
    }
    else if (mode == 1)
    {
        // Random flashes
        randomSeed(mm);
        for (i = 0; i < NUM_LEDS; i++)
        {
            if (random8(200) == 0)
            {
                leds[i] = CHSV(25, 255, 100);
            }
        }
    }
}

// ---------------------------------
// ----------- JOYSTICK ------------
// ---------------------------------
void getInput()
{
    // This is responsible for the player movement speed and attacking.
    // You can replace it with anything you want that passes a -90>+90 value to joystickTilt
    // and any value to joystickWobble that is greater than ATTACK_THRESHOLD (defined at start)
    // For example you could use 3 momentery buttons:
    // if(digitalRead(leftButtonPinNumber) == HIGH) joystickTilt = -90;
    // if(digitalRead(rightButtonPinNumber) == HIGH) joystickTilt = 90;
    // if(digitalRead(attackButtonPinNumber) == HIGH) joystickWobble = ATTACK_THRESHOLD;

    ///// PLAYER 1 ///////

    accelgyroIC1.getMotion6(&ax1, &ay1, &az1, &gx1, &gy1, &gz1);
    int a = (JOYSTICK_ORIENTATION == 0 ? ax1 : (JOYSTICK_ORIENTATION == 1 ? ay1 : az1)) / 166;
    int g = (JOYSTICK_ORIENTATION == 0 ? gx1 : (JOYSTICK_ORIENTATION == 1 ? gy1 : gz1));
    if (abs(a) < JOYSTICK_DEADZONE)
        a = 0;
    if (a > 0)
        a -= JOYSTICK_DEADZONE;
    if (a < 0)
        a += JOYSTICK_DEADZONE;
    MPUAngleSamples.add(a);
    MPUWobbleSamples.add(g);

    joystickTilt = MPUAngleSamples.getMedian();
    if (JOYSTICK_DIRECTION == 1)
    {
        joystickTilt = 0 - joystickTilt;
    }
    joystickWobble = abs(MPUWobbleSamples.getHighest());
}

////////// PLAYER 2 /////////

void getInput2()
{
    accelgyroIC2.getMotion6(&ax2, &ay2, &az2, &gx2, &gy2, &gz2);
    int a2 = (JOYSTICK_ORIENTATION == 0 ? ax2 : (JOYSTICK_ORIENTATION == 1 ? ay2 : az2)) / 166;
    int g2 = (JOYSTICK_ORIENTATION == 0 ? gx2 : (JOYSTICK_ORIENTATION == 1 ? gy2 : gz2));
    if (abs(a2) < JOYSTICK_DEADZONE)
        a2 = 0;
    if (a2 > 0)
        a2 -= JOYSTICK_DEADZONE;
    if (a2 < 0)
        a2 += JOYSTICK_DEADZONE;

    MPUAngleSamples2.add(a2);
    MPUWobbleSamples2.add(g2);
    joystickTilt2 = MPUAngleSamples2.getMedian();

    if (JOYSTICK_DIRECTION == 1)
    {
        joystickTilt2 = 0 - joystickTilt2;
    }
    joystickWobble2 = abs(MPUWobbleSamples2.getHighest());
}

////////////////////////////////////////////////////////

// ---------------------------------
// -------------- SFX --------------
// ---------------------------------
void SFXtilt(int amount)
{
}
void SFXattacking()
{
    wTrig.trackPlayPoly(2);
    // wTrig.stopAllTracks();
    // // wTrig.samplerateOffset(0);
    // // wTrig.trackGain(1, 20);
    // wTrig.trackPlayPoly(1);
    // wTrig.trackPlayPoly(7);

    // wTrig.trackGain(1, 20);
    // wTrig.trackPlayPoly(1);
}
void SFXdead()
{
    // wTrig.samplerateOffset(0);
    // wTrig.masterGain(0);
    wTrig.stopAllTracks();
    wTrig.trackPlayPoly(3);
    // wTrig.samplerateOffset(0);
    // wTrig.stopAllTracks();
    // wTrig.trackPlayPoly(1);
}
void SFXkill()
{
    // wTrig.trackPlayPoly(2);
}
void SFXwin()
{
    // wTrig.samplerateOffset(0);
    // wTrig.masterGain(0);
    wTrig.stopAllTracks();
    wTrig.trackPlayPoly(1);
}

void SFXcomplete()
{
    wTrig.stopAllTracks();
    wTrig.trackPlayPoly(1);
}

void SFXGameOver()
{
    wTrig.stopAllTracks();
    wTrig.trackPlayPoly(1);
}