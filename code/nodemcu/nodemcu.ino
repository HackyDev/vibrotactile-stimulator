// https://lastminuteengineers.com/creating-esp8266-web-server-arduino-ide/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

const char* ssid = "vtstim";  // Enter SSID here
const char* password = "12345678";  // Enter Password here

enum State {
  NONE,
  STEP,
  VIBRATION,
  PULSE_ON,
  PULSE_OFF,
  PAUSE
};

enum FingerPin {
  RIGHT_INDEX_FINGER_PIN = D3,
  RIGHT_MIDDLE_FINGER_PIN = D2,
  RIGHT_RING_FINGER_PIN = D1,
  RIGHT_LITTLE_FINGER_PIN = D0,
  LEFT_INDEX_FINGER_PIN = D7,
  LEFT_MIDDLE_FINGER_PIN = D6,
  LEFT_RING_FINGER_PIN = D5,
  LEFT_LITTLE_FINGER_PIN = D4,
};

struct UserSettings {
  int vibrationFrequencyHz;
  int vibrationDurationMs;
  int pauseDurationMs;
  int onPeriodAmountOfSteps;
  int offPeriodAmountOfSteps;
  int jitterPercent;
  bool mirrorModeEnabled;
};

class RandomProvider {
  private:
    int _sequencePosition = 0;
    long _sequenceStorage[3] = { 0, 0, 0 };
  public:
    long getRandomNumber () {
      long rand = random(1, 5);
      if (_sequenceStorage[0] == rand || _sequenceStorage[1] == rand || _sequenceStorage[2] == rand) {
        return getRandomNumber();
      } else {
        _sequenceStorage[_sequencePosition] = rand;
        if (++_sequencePosition > 2) {
          _sequenceStorage[0] = 0;
          _sequenceStorage[1] = 0;
          _sequenceStorage[2] = 0;
          _sequencePosition = 0;
        }
        return rand - 1;
      }
    }
};

class Timer {
  private:
    unsigned long _startedAt;
    unsigned long _endsAt;
    unsigned long _duration;
    bool _hasOverflowed;
    bool _isMillis;
  unsigned long getTime () {
    if (_isMillis) {
      return millis();
    } else {
      return micros();
    }
  }
  public:
    Timer (bool isMillis) {
      _isMillis = isMillis;
    }
    const unsigned long& startedAt = _startedAt;
    void start(unsigned long durationMicroseconds) {
      _startedAt = getTime();
      _endsAt = _startedAt + durationMicroseconds;
      _duration = durationMicroseconds;
      _hasOverflowed = _endsAt < _startedAt;
    }
    bool isStarted () {
      return _duration > 0;
    }
    unsigned long getRunTime () {
      if (isStarted()) {
        return getTime() - _startedAt;
      } else {
        return 0;
      }
    }
    unsigned long isDone () {
      if (_duration == 0) {
        return false;
      }
      unsigned long now = getTime();
      if (_hasOverflowed) {
        return now + _duration > _startedAt;
      } else {
        return now > _endsAt;
      }
    }
    void reset () {
      _startedAt = 0;
      _endsAt = 0;
      _duration = 0;
      _hasOverflowed = false;
    }
};

class UserSettingsStorage {
  private:
    UserSettings _defaultUserSettings;
  public:
    int checkValue = 11111;
    int checkAddress = 100;
    int dataAddress = 200;

    UserSettingsStorage (UserSettings defaultUserSettings) {
      _defaultUserSettings = defaultUserSettings;
    }
    bool put(UserSettings userSettings) {
      EEPROM.put(dataAddress, userSettings);
      EEPROM.put(checkAddress, checkValue);
      return EEPROM.commit();
    }
    UserSettings get () {
      UserSettings userSettings;
      int checkValueStored;
      EEPROM.get(checkAddress, checkValueStored);
      if (checkValue == checkValueStored) {
        EEPROM.get(dataAddress, userSettings);
        return userSettings;
      } else {
        return getDefaultSettings();
      }
    }
    UserSettings getDefaultSettings () {
      return _defaultUserSettings;
    }
    void printUserSettings (UserSettings userSettings) {
      Serial.println("");
      Serial.print("vibrationFrequencyHz:");
      Serial.println(userSettings.vibrationFrequencyHz);
      Serial.print("vibrationDurationMs:");
      Serial.println(userSettings.vibrationDurationMs);
      Serial.print("mirrorModeEnabled:");
      Serial.println(userSettings.mirrorModeEnabled);
      Serial.print("onPeriodAmountOfSteps:");
      Serial.println(userSettings.onPeriodAmountOfSteps);
      Serial.print("offPeriodAmountOfSteps:");
      Serial.println(userSettings.offPeriodAmountOfSteps);
      Serial.print("jitterPercent:");
      Serial.println(userSettings.jitterPercent);
      Serial.print("mirrorModeEnabled:");
      Serial.println(userSettings.mirrorModeEnabled);
    }
};

class App {
  private:
    State _state = State::NONE;
    // TODO ADD TYPE CHECK to UserSettingsStorage
    UserSettingsStorage _userSettingsStorage = UserSettingsStorage({ 0, 0, 0, 0, 0, 0, false });
    UserSettings _userSettings = _userSettingsStorage.getDefaultSettings();
    unsigned long _pulseOnDurationUs = 0;
    unsigned long _pulseOffDurationUs = 0;
    int _leftHandActivePin = 0;
    int _rightHandActivePin = 0;
    int _step = 0;
    bool _isOnPeriod = true;
    long _jitterValueMu = 0;
    long _currentJitterValueMu = 0;
    Timer _muTimer = Timer(false);
    Timer _muTimer2 = Timer(false);
    RandomProvider _randomProvider = RandomProvider();
    int _fingerPins[2][4] = {
      { 
        LEFT_INDEX_FINGER_PIN,
        LEFT_MIDDLE_FINGER_PIN,
        LEFT_RING_FINGER_PIN,
        LEFT_LITTLE_FINGER_PIN
      },
      {
        RIGHT_INDEX_FINGER_PIN,
        RIGHT_MIDDLE_FINGER_PIN,
        RIGHT_RING_FINGER_PIN,
        RIGHT_LITTLE_FINGER_PIN
      }
    };
    int _fingerPinsReversed[2][4] = {
      { 
        LEFT_LITTLE_FINGER_PIN,
        LEFT_RING_FINGER_PIN,
        LEFT_MIDDLE_FINGER_PIN,
        LEFT_INDEX_FINGER_PIN
      },
      {
        RIGHT_LITTLE_FINGER_PIN,
        RIGHT_RING_FINGER_PIN,
        RIGHT_MIDDLE_FINGER_PIN,
        RIGHT_INDEX_FINGER_PIN
      }
    };
    String _stateStr[6] = {
        "NONE",
        "STEP",
        "VIBRATION",
        "PULSE_ON",
        "PULSE_OFF",
        "PAUSE"
    };
    void applyUserSettings (UserSettings userSettings) {
      _pulseOnDurationUs = 1000000 / (userSettings.vibrationFrequencyHz * 2);
      _pulseOffDurationUs = 1000000 / (userSettings.vibrationFrequencyHz * 2);
    }
    unsigned long getPauseDuration () {
      return (_userSettings.pauseDurationMs * 1000) + _currentJitterValueMu;
    }
    unsigned long getVibrationDuration () {
      return _userSettings.vibrationDurationMs * 1000;
    }
    void setJitterValue () {
      _jitterValueMu = ((_userSettings.pauseDurationMs * 1000) / 2) * (_userSettings.jitterPercent / 100);
    }
    void updateCurrentJitterValue () {
      if (_step % 2 > 0) {
        long rand = random(0, 2);
        if (rand > 0) {
          _currentJitterValueMu = _jitterValueMu;
        } else {
          _currentJitterValueMu = -_jitterValueMu;
        }
      } else {
        if (_currentJitterValueMu > 0) {
          _currentJitterValueMu = -_jitterValueMu;
        } else {
          _currentJitterValueMu = _jitterValueMu;
        }
      }
    }
    bool updateState () {
      State state = _state;
      switch (_state) {
        case State::NONE:
          _state = State::STEP;
          break;
        case State::STEP:
          _state = State::VIBRATION;
          _muTimer2.start(getVibrationDuration());
          break;
        case State::VIBRATION:
          _state = State::PULSE_ON;
          _muTimer.start(_pulseOnDurationUs);
          break;
        case State::PULSE_ON:
          if (_muTimer2.isDone()) {
            _state = State::PAUSE;
            _muTimer2.reset();
            _muTimer.reset();
          } else if (_muTimer.isDone()) {
            _state = State::PULSE_OFF;
            _muTimer.reset();
            _muTimer.start(_pulseOffDurationUs);
          }
          break;
        case State::PULSE_OFF:
          if (_muTimer2.isDone()) {
            _state = State::PAUSE;
            _muTimer2.reset();
            _muTimer.reset();
          } else if (_muTimer.isDone()) {
            _state = State::PULSE_ON;
            _muTimer.reset();
            _muTimer.start(_pulseOnDurationUs);
          }
          break;
        case State::PAUSE:
          if (_muTimer2.isStarted()) {
            if (_muTimer2.isDone()) {
              _state = State::STEP;
              _muTimer2.reset();
            }
          } else {
            _muTimer2.start(getPauseDuration());
          }
          break;
      }
      return state != _state;
  }
  void updateActivePins () {
    int num = _randomProvider.getRandomNumber();
    _leftHandActivePin = _fingerPins[0][num];
    if (_userSettings.mirrorModeEnabled) {
      _rightHandActivePin = _fingerPins[1][num];
    } else {
      _rightHandActivePin = _fingerPinsReversed[1][num];
    }
  }
  void newStepHandler () {
    if (_isOnPeriod) {
      if (_step >= _userSettings.onPeriodAmountOfSteps) {
        _isOnPeriod = false;
        _step = 0;
      }
    } else {
      if (_step >= _userSettings.offPeriodAmountOfSteps) {
        _isOnPeriod = true;
        _step = 0;
      }
    }
    updateActivePins();
    updateCurrentJitterValue();
    _step++;
  }
  void initPins () {
    for (int i = 0; i < 4; i++) {
      pinMode(_fingerPins[0][i], OUTPUT);
      pinMode(_fingerPins[1][i], OUTPUT);
    }
  }
  void printState (State state) {
    Serial.print("STATE: ");
    Serial.println(_stateStr[state]);
  }
  public:
    void setup () {
      randomSeed(analogRead(A0));
      EEPROM.begin(512);
      _userSettings = _userSettingsStorage.get();
      _userSettingsStorage.printUserSettings(_userSettings);
      applyUserSettings(_userSettings);
      
      setJitterValue();
      initPins();
    }
    void update () {
      bool isStateChanged = updateState();
      if (isStateChanged) {
        switch (_state) {
          case State::STEP:
            newStepHandler();
            break;
          case State::PAUSE:
            digitalWrite(_leftHandActivePin, LOW);
            digitalWrite(_rightHandActivePin, LOW);
            break;
          case State::PULSE_ON:
            if (_isOnPeriod) {
              digitalWrite(_leftHandActivePin, HIGH);
              digitalWrite(_rightHandActivePin, HIGH);
            }
            break;
          case State::PULSE_OFF:
            digitalWrite(_leftHandActivePin, LOW);
            digitalWrite(_rightHandActivePin, LOW);
            break;
        }
      }
    }
    bool setUserSettings (UserSettings userSettings) {
      bool success = _userSettingsStorage.put(userSettings);
      if (success) {
        _userSettings = userSettings;
        applyUserSettings(_userSettings);
      }
      return success;
    }
    UserSettings getUserSettings () {
      return _userSettings;
    }
    
  bool setUserSettingsFromHttpRequest (ESP8266WebServer &server) {
    UserSettings userSettings = getUserSettings();
    for (int i = 0; i < server.args(); i++) {
      String argValue = server.arg(i);
      String argName = server.argName(i);
      if (argName == "vibrationFrequencyHz") {
        userSettings.vibrationFrequencyHz = argValue.toInt();
      } else if (argName == "vibrationDurationMs") {
        userSettings.vibrationDurationMs = argValue.toInt();
      } else if (argName == "pauseDurationMs") {
        userSettings.pauseDurationMs = argValue.toInt();
      } else if (argName == "onPeriodAmountOfSteps") {
        userSettings.onPeriodAmountOfSteps = argValue.toInt();
      } else if (argName == "offPeriodAmountOfSteps") {
        userSettings.offPeriodAmountOfSteps = argValue.toInt();
      } else if (argName == "jitterPercent") {
        userSettings.jitterPercent = argValue.toInt();
      } else if (argName == "mirrorModeEnabled") {
        userSettings.mirrorModeEnabled = argValue == "true";
      }
    }
    return setUserSettings(userSettings);
  }
};

App app = App();

// SERVER
IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  // SERVER
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  delay(100);
  server.on("/", serveIndex);
  server.on("/update", serveUpdate);
  server.onNotFound(serveIndex);
  server.begin();

  // APP
  app.setup(); // return mode and disable/prevent init wifi and server if needed (for performance)
}

void loop() {
  server.handleClient();
  app.update();
}

// SERVER

void serveIndex() {
  server.send(200, "text/html", getUpdateUserSettingsFormHtml(app.getUserSettings()));
}
void serveUpdate() {
  bool success = app.setUserSettingsFromHttpRequest(server);
  if (success) {
    server.send(200, "text/html", getUpdateUserSettingsSuccessHtml()); 
  } else {
    server.send(200, "text/html", getUpdateUserSettingsFailHtml());
  }
}

// https://flems.io/#0=N4IgtglgJlA2CmIBcBmArAOgGwHYA0IAzvAgMYAu8UyIGphhIBpA9gHaEsI2uywCGAB2LUChUgCcusZAG0ADHnkBdAgDMICRklmg2-MIiS0AFuTAzm7Sm3I0APIQBuAcwAE0ALwAdEBDZqLAC0EGAuvm4AHhYcPiBm5IJIAPTJAO4ZGGkoGCwSLskATPIlyc7hIFExhEiRsP4A1nEJSakZaVk5eQUAjACcA8l1jRFp0OQmcSgALCURJvAQLmZTs-IRThDwaQBCLJFx8m5HABwDGGgnbmd9Fye+AHzebM9u9u5evvyPr-aC-BM3FA4gBZHooW5YNBuaZoQoYHAnExoW7TPqFJyFFDyDAoAAS4MhaAAXhFyBJ+BxAhIwHFyZTCAJKAAKII9HAYYrTNxBCHw6YASl8ySebDe-0BGj4cQAxGp5RFgb4wbNsNDYTlEciOWiMYVCidcQTVVDSZV6VS8rTfBbGQD4Kz2Zz5NzeeiMILhaLxQCTG4pbBZTB4KQAEaFRWg9mYHrcjUIpFYjAlaaYg1G6MYWNmty26nWkC2pkOtkcrk8vkeoUgEW-ZIub3vDxKkCkH5iv6+oFRiFqmFwhPI1HozHYjO901kimWml06d2lngrAYdE8s7YeSFau1jsSv0BoNQEPhyPK2M4qH9zVItA6kf6w3489qnN5q1zhnF5lLleFNeQzdt0betG3cQhyRYBp4CCeo2BDIQ4kIABHABXfgJHgCJwKkKCgjGKAJjieEsFhe4QFA-1rCCQgIGJeAiMwaZ2zeShInIKI4hOTAcBweQUQiABPVZlxOCEeh6CJAlsPDFmWcg4lDLgoAiB57HIQh-jFA5fC4hFeP4yohN8GYRLEiTKgeAB5AA5NwAAUAFEACUAElLIAEXsZJ1M01TvPgNjG1Y9jtJAZ9ET6FAI0MuIsHTHAUGjSTNEDXwwHQlh2DJALyCCSlSBMPI4kgGAEEeNSNMpDjfHCs4osE2L4sSnA0EeGygkEeAJAgFgoDebzKrYVSfKq0Lasi6K3CMkAsCwHFphOJLyIMFgUNsNwWDUNxwPgYQvJGob9pyoKcuqkAIUfE5ikm6a4sNBKlv9FLioyrLzRyvK2AKoq0ugOBMPIirNLOi7cSuzcGt8O6EWa1ryMsgAxBGOq6nq+v2wbhsGkG+ku67IZmuaPUWlrHhWtb2M27bKD2gbfKOwK6wbX53Ck3KaLooj4VvMiTrYs7bxxM40FhAnLhyE40Swcy3Cx4HQsFjBhdFmLfHFpWpfMh4ACoACsIHISgJA8Qg3H4NwnHQiAAR6sUqc2UMKXIW3+oOuXRriRXlbhqbYp6eF5BwHpJceDaOHgdjncMDH6YOgXeKVvoRZ927VUKPppnBR49YNo2TbNwRBHqKhcxYV3MaBj21YT72Cdmw00B6LAUCwUP4H4Arqd2twxkBc3pygFgwHL2PsYVmuk5V32ob6ZcEq4vpQ5olwxWZABqIIBRjyk-OCvmQs9tBMBQSXplb1WQCPnIsVEpjKjZmSlhWXxFNgZSLMrrTD+P0-z+ny+0DXxPjMSSVEOb0V8IURioDpJpFks-EAbArT8FgOVN2QQR470-k9aUaVXovHIvrQ2XU3CdQkKQeAtht5DUwYdfyjMOzBQFtMPSGck4ExOPITU2Im4qWwQrFhPE2Ep04lwmG8heHkW1mkBYYpKH8FDAgPqEx4AWwgI7G27B85CCLlsZRZdWASAwhpdgUB-Cs3MV1U2Wj26dxMJSKAGBaHuy-mrQROBhEcMSiuOEEJs4yMoUCCAhAFFKNzAsDaEgjzG2CW4DCTgrFUDwG4eAGAXAYGSQEsUVV-BHkiE9NgLgSGxPAihMAKFix9S0dQlxzDWFohETpIm7Iz683CaohAah2L2LYMoiJ9RDYIAKUUmJptwKhAqfaKpYoVFxKft0hxGAan8M9u4zxF8TibNxLPPo5kRQM3IA8NwTMKIP3AQxD0MDcpwPmQpJSzE1KnVCjgdMwcEo3U9k6cEi1oq1Oea8k47yxZfJPv7bKbFPrfQkMVP6ZVyIACkXIABUkXOWWfQw5vwmGhVhII6YD4CYtRyE3Fqd9cwfXyoVaFv1SoAz+XEXFCJ8UGkJYArMuARaPBBC5JyTlLJOTcCCDyDl0V7yxU8uI-scQogWovC+Z8Vx9E3DgPhccxrFAuBnM4BMFUDGVSpAAyqiuyorjriv5qFQosZcSZ3YRfcS8I9WIlVWPIi1qZj9EaWFf2irA5kVlnZAAggAVQNSKumWCMX7xBlahEEjCh-2miZJWZkXXyymLG3i-tE3CRTbsmWVkkb2Wcm5TyEa6Fio7CBE5vw9w4NSog9gANqY4QgSAGUKAWo4CgJNbCkFoKwXgoIRCqF0LNr7bhfChFIHYFIqecA7IcRQLcD0eQy4URuBQDkTcPRcSbhAZUHcPpASfBAKGZKuD22hjWHMSoLYwRJmmNydkhp04mBmMTE4ThbzJhwNMAkj7pg5iPU2CdA7-BDpHWhDCWEIKTvGJMGdJFLgPOxURF0Fx5C4wJla-k4JpgqvvmA2ibam4ri3Vcx+ck7lvzTVXEAXJMPYYvrhj0+HCOy2wWckjFy74PAAGouR2E5QNSK3LWRqc4g50bLU9EwHFK1cr-6FFVE+3ik1uOc0gdAj+aq3Xyf1P0HDqmCMQ3Ihgrjz08FSDeg8B2TsXZqAwqhShpABJuGZHiAAWlvctu8K56cgXJ7AhmlPTUKC8rMOy5UPAs3HA81nMoELs+ohzWioAoTS6vEEBqBRSb89J81B8gu3C4kfMLkqT4XGbuySj5ztOXKI7A+B8kX73N066krSsj4GX-t86rzUL0NvSjZghlFmu3N8EgmkKDHgYP+ChYgQJMuaOy7l01DD+rM13F2BLja4KwdbbKTtt4e2Hf7TBCDpAEK+GQtB8dcHoJTsQwx2dKG73FWfFgPobhigciwFcH9Ca+hBDjTgAHEQQOLdUcMNgDQkAmAwmoWU3xzTznzB+DgX4gi7IRCpzdm4ER9CAr8aHURYLw8R-AZHvgZTnrRwyDHNp5zY9xy87k2IcQuhJx2MnsPKdI5R1ORn75mefntKyMcCaUAE-hB4nnbw+cU4R4L2nqPmxxDUMLmcBYiwS+8f0K42I5fE69KTpb-OVfU9lPT3M6PReFhZxL3kS7m6-fxSuHACv7BK8aFbmn7b1dvlnGLrHEv3RQh+8bon3vfdw-9zb7XhAmeO-FyyA3uNN0-vErHi3yuqcB7p0nlPeuWQR43cHTAZxc8w-z6r9tbZKinvfnbkXIfU9h5ZNLD00sa-k79wXoMERT3NuD7rp3Xfd1nx6H3y3g-aej-t+30vDpu-T9n3X63tOtcM515jhcDpYS3H6DyS4LDRJ5fECgh0u6PF9C92b3neeB-17lMXh3K-mToBYfjoIZ+U2X7XYIDfhE737e51qnoVAtr9qyjfZwFKZgZBBQD8CED2JGL8DTQsKrriTJKvo7ocaIHIGoGbRqDECtYzTVbBzna4SDrXbDq3ajowaVCIHPYMpZgSIyz3rgychYBuAJxn5OCZxCx-xQ7P7x7z7tpQFj777Y6roxg9A8jJglB8Qb4v5b7tomDv7L4T4lhyFZgKFsiBy-iqHiGv4QBaHj5p4liiQrhnw8irocjojgFdinqaH3xWbtryj3735DZBAsD-CkAGzTQXCxjpzzoghBwxjQido5BwgmB6HiROAOG-h4iRFZgkiQ6NgQEtjmHuGXp043rrAfbKgvLLi7KboJQXCFAmBxTJjfZJFGHoipEJorg9DAaiheTlAdH5hmwUC2xxDJAoSCDIGUDMRvBmJODehuDTGOC+T8aparb+jOYoSubuaeY+adG+SvDTFvD+CCAoTsT6CGBxD2arYIzLGrF4g5jLEQAYR9RFwdzwCFRvxdRxAOS2AkKWywArERCQBsCHC-H8ChRRTg4cbkACSdRxBsBlKhhdSwa7SSqP4zHJATFTH2ConbEzGYzzEaLOzpYrZ4lra+aYyYm7FsD7GHEGBtqnGEk7CZbgTuQEm2wgiEARA3F3GkICAULPHRJvEfHGxfE-GVB-EAnClAkZqglkgQltrQlgCwkSDwn0FhRIn9QYlijjEQCTGkmzE7x2T8Bk4ZZZYeY5bElbHqkzF7EHFuBHFtoLbECMlZYslsnwCoS3ElwPHclKSvG+DvF5yCnNoim+BFFuDpTAkRa4BSmQlTYwlwnME0yIk1hokomalTHbHokpnamYztRkJoxmxgCrTrRUw7S0wknmlkkUnWlUlxDsB2Soy9SBr5kUyWRqAGo0ysmVDsnulclPFenUogC+mfEoJCkhn+CikhninGThlgnSlQkxkKlxkIk1QqleRqnTFpmrk7E6lDSIzIw5m9R5kFmUxbTFmECbE7zamWmUnHG+AkG1ndT1mNm2DNmtm7TtlxIukoRun3Hdk8nen9n8kWxDkBmjlBmAlhmSnmgznRlymxldxKl7JJkbnrkZlllbkPDwq5wkJkIUJUJlBmk7HllWk2lxBEJGx3k4XkDOmukckek9kvF9kDkClAW-EgUgDBmhmSq3q5hQWIJzmKkJlHqqkoVrnqnplamoWYwgi3FSDGz5lHhnlDSZkkAhhXltqQBGJ5Agi9TwDvGhJUBjEEX2D+GEmAXfFtrkhCkPC6WKJUBeTGW2xTGGX2VaL+ma4oLECPDuTBJ6VQB2WCCEmOX9TEBkCYqiXJniUiUWnklWnglRlEAoShiQCUWVCuW+DBrDH2jtheT5gPBMBEDKV9HsDaBsVID9BBDyBIA4AgAAC+eAegVJNAGAusjAVgHxtgNANVdViCDVxgdADAeVrAbVdgxgikUA7m2sbgwA45+Q-gSAxwuIGEYAAA3COWwHhAhnNauvIIIJECtdVc8KNe5lNWYhpAIAJHNWoAgLtf6FdXhBSEkD3PdStW4LrIts7GoAJEEINTYOQHNRRV1CtSgksGtQbPAGADUDdQFNROQOhOQCtQ-GoAYJoOdW4EivwIVOlEtftQEFaJNUEqdRgRdVdStaGetQRCYHNWgCUDtStQsPMnNfqeQCwPDbdWYhhIVWwH9VwGUmwCtaGB3A0C4FIGtFAHNTIqDYQPmVBHzXkNEptTtdtFwNAG4IogLSteLZQNRAEfAHNUgmkE9djT0UchMZNSdQ8SjZdQFCta9eMh9V9dYJQr9W4P9RICzVDfrUIGLQbc8EbUEk4LLG4NrMkj0cFSpXjc9nNcUNtddYpJENRLROYnNYpFEl1EELHXtc8DKP4IECEGEOHRtSuiUAAKS00tYM0HHM3jlx0R1uBU3R180C1C0Fmi09wmAS1S3wAy0p0SDy35KcD1B9Sq2kANB7V5Wh0c3FWFBICbI1XKDVVAA

String getUpdateUserSettingsFormHtml (UserSettings userSettings) {
  String ptr = "<!DOCTYPE html> <html>";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">";
  ptr += "<title>Vibrotactile stimulator settings</title>";

  ptr += "<style>";
  ptr += "body * { margin: 0.3rem; min-width: 100px; }";
  ptr += "body { display: flex; flex-wrap: wrap;  justify-content: center; align-items: flex-start; font-family: Tahoma;}";
  ptr += "form { display: flex; max-width: 500px; height: auto; flex-direction: column; background: whitesmoke; border: 1px solid black; white-space: nowrap;}";
  ptr += "form > div {display: flex; justify-content: center; flex-wrap: wrap;}";
  ptr += "form > div > *, form select { width: 200px; box-sizing: border-box; }";
  ptr += "#info-img { width: 100%; height: auto; max-width: 500px; background-color: whitesmoke; border: 1px solid black; }";
  ptr += "</style>";

  ptr += "<svg id=\"info-img\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"3400\" height=\"3400\" viewBox=\"0 0 899.58 899.58\"><g id=\"a\"><path d=\"M122.446 60.38h59.492v230.3h-59.492z\"/><path fill=\"#fff\" d=\"M123.446 61.38h57.492v228.3h-57.492z\"/><path fill=\"#ddecb2\" d=\"M157.936 61.38h23.004v228.3h-23.004z\"/></g><g id=\"c\"><path d=\"M276.57 363.178h59.492v230.3H276.57z\"/><path fill=\"#ddecb2\" d=\"M277.57 364.178h57.492v228.3H277.57z\"/></g><g stroke-linecap=\"square\" stroke-width=\"2.6458\"><g font-size=\"25.4\"><text x=\"85.77059\" y=\"346.83911\" font-weight=\"bold\"><tspan x=\"85.77059\" y=\"346.83911\">ON PERIOD</tspan></text><text x=\"140.78932\" y=\"628.73175\" fill=\"maroon\" text-anchor=\"middle\"><tspan x=\"140.78932\" y=\"628.73175\">ON-period </tspan><tspan x=\"140.78932\" y=\"660.48175\">amount of steps</tspan></text><text x=\"398.38202\" y=\"628.73175\" fill=\"maroon\" text-anchor=\"middle\"><tspan x=\"398.38202\" y=\"628.73175\">OFF-period </tspan><tspan x=\"398.38202\" y=\"660.48175\">amount of steps</tspan></text></g><g font-size=\"22.578\"><text x=\"570.89545\" y=\"583.84961\"><tspan x=\"570.89545\" y=\"583.84961\">*jitter is a variation of vibration </tspan><tspan x=\"570.89545\" y=\"612.07184\"> onset time</tspan><tspan x=\"570.89545\" y=\"640.29413\">*jitter is applied to </tspan><tspan x=\"570.89545\" y=\"668.51636\"> each step with a random </tspan><tspan x=\"570.89545\" y=\"696.73859\"> sign (+-)</tspan></text><text x=\"555.38446\" y=\"553.23834\" font-weight=\"bold\"><tspan x=\"555.38446\" y=\"553.23834\" font-size=\"25.4\" font-weight=\"normal\"><tspan>- </tspan><tspan fill=\"maroon\">jitter percent</tspan> </tspan></text><text x=\"54.77949\" y=\"803.73016\"><tspan x=\"54.77949\" y=\"803.73016\">*when enabled the vibration is applied to corresponding fingers on each hand. </tspan><tspan x=\"54.77949\" y=\"831.95239\">*when disabled the order is reversed, e.g., when an index finger is stumulated on </tspan><tspan x=\"54.77949\" y=\"860.17468\"> the left hand the little finger is stimulated on the right hand.</tspan></text></g><g font-size=\"25.4\" font-weight=\"bold\"><text x=\"728.18732\" y=\"517.13812\"><tspan x=\"728.18732\" y=\"517.13812\" text-anchor=\"middle\">JITTER</tspan></text><text x=\"454.74228\" y=\"753.16754\" text-anchor=\"middle\"><tspan x=\"454.74228\" y=\"753.16754\">MIRROR MODE</tspan></text><text x=\"120.59489\" y=\"46.99027\"><tspan x=\"120.59489\" y=\"46.99027\">STEP</tspan></text><text x=\"214.34195\" y=\"112.99078\"><tspan x=\"214.34195\" y=\"112.99078\">PAUSE</tspan></text><text x=\"321.70126\" y=\"346.83911\"><tspan x=\"321.70126\" y=\"346.83911\">OFF PERIOD</tspan></text></g></g><path fill=\"none\" stroke=\"#3757d2\" stroke-linecap=\"square\" stroke-width=\"2.6458\" d=\"m170.25 106.59 33.021.30234\"/><path id=\"b\" fill=\"#b40000\" d=\"M123.44 178.29h34.488v57.074H123.44z\"/><g stroke-linecap=\"square\" stroke-width=\"2.6458\"><text x=\"204.5098\" y=\"212.41347\" font-size=\"16.933\" font-weight=\"bold\"><tspan x=\"204.5098\" y=\"212.41347\"><tspan font-size=\"25.4\">VIBRATION</tspan> </tspan></text><text x=\"215.62219\" y=\"240.44702\" font-size=\"25.4\"><tspan x=\"215.62219\" y=\"240.44702\">- <tspan fill=\"maroon\">vibration frequency (HZ)</tspan></tspan><tspan x=\"215.62219\" y=\"272.19699\">- <tspan fill=\"maroon\">vibration duration (MS) </tspan></tspan></text><text x=\"219.85559\" y=\"138.56317\" font-size=\"25.4\" font-weight=\"bold\"><tspan x=\"219.85559\" y=\"138.56317\" fill=\"maroon\" font-weight=\"normal\">- pause duration (MS)</tspan></text></g><path fill=\"none\" stroke=\"#3757d2\" stroke-linecap=\"square\" stroke-width=\"2.6458\" d=\"m140.69 207.68 57.269-.70768\"/><use xlink:href=\"#a\" transform=\"translate(-91.724 302.79)\"/><use xlink:href=\"#b\" transform=\"translate(-91.724 300.04)\"/><use xlink:href=\"#a\" transform=\"translate(-30.263 302.79)\"/><use xlink:href=\"#a\" id=\"f\" transform=\"translate(31.198 302.79)\"/><use xlink:href=\"#b\" transform=\"translate(-30.263 242.97)\"/><use xlink:href=\"#a\" transform=\"translate(92.659 302.79)\"/><use xlink:href=\"#b\" transform=\"translate(31.198 357.11)\"/><use xlink:href=\"#b\" transform=\"translate(92.659 185.89)\"/><use xlink:href=\"#c\" id=\"d\" transform=\"translate(61.461)\"/><use xlink:href=\"#d\" id=\"e\" transform=\"translate(61.461)\"/><use xlink:href=\"#e\" transform=\"translate(61.461)\"/><use xlink:href=\"#f\" transform=\"translate(459.19 -584.83) scale(1.7997)\"/><use xlink:href=\"#f\" transform=\"translate(354.24 -584.83) scale(1.7997)\"/><path id=\"g\" stroke=\"#696969\" stroke-dasharray=\"4.1011, 8.20217\" stroke-dashoffset=\"6.5618\" stroke-linecap=\"square\" stroke-width=\"4.1011\" d=\"M820.26 70.584v410.86\"/><use xlink:href=\"#g\" transform=\"translate(-105.11 -.00005)\"/><use xlink:href=\"#h\" transform=\"translate(-105.11 -107.92)\"/><use xlink:href=\"#i\" transform=\"translate(-83.946 -107.92)\"/><path id=\"h\" fill=\"#ff9797\" fill-opacity=\".51429\" d=\"M715.15 373.52h105.11v107.92H715.15z\"/><path id=\"i\" fill=\"#b40000\" d=\"M726.91 373.52h62.069v107.92H726.91z\"/></svg>";

  ptr += "</style>";
  ptr += "</head>";
  ptr += "<body>";

  ptr += "<form action=\"/update\">";
  ptr += "<div>";
  ptr += "<span>Vibration frequency (HZ)</span>";
  ptr += "<input value=\"" + String(userSettings.vibrationFrequencyHz) + "\" name=\"vibrationFrequencyHz\" required placeholder=\"Enter value\" min=\"0\" max=\"32767\" type=\"number\" step=\"1\"/>";
  ptr += "</div>";
  ptr += "<div>";
  ptr += "<span>Vibration duration (MS)</span>";
  ptr += "<input value=\"" + String(userSettings.vibrationDurationMs) + "\" name=\"vibrationDurationMs\" required placeholder=\"Enter value\" min=\"0\" max=\"32767\" type=\"number\" step=\"1\"/>";
  ptr += "</div>";

  ptr += "<div>";
  ptr += "<span>Pause duration (MS)</span>";
  ptr += "<input value=\"" + String(userSettings.pauseDurationMs) + "\" name=\"pauseDurationMs\" required placeholder=\"Enter value\" min=\"0\" max=\"32767\" type=\"number\" step=\"1\"/>";
  ptr += "</div>";

  ptr += "<div>";
  ptr += "<span>ON-period amount of steps</span>";
  ptr += "<input value=\"" + String(userSettings.onPeriodAmountOfSteps) + "\" name=\"onPeriodAmountOfSteps\" required placeholder=\"Enter value\" min=\"0\" max=\"32767\" type=\"number\" step=\"1\"/>";
  ptr += "</div>";

  ptr += "<div>";
  ptr += "<span>OFF-period amount of steps</span>";
  ptr += "<input value=\"" + String(userSettings.offPeriodAmountOfSteps) + "\" name=\"offPeriodAmountOfSteps\" required placeholder=\"Enter value\" min=\"0\" max=\"32767\" type=\"number\" step=\"1\"/>";
  ptr += "</div>";

  ptr += "<div>";
  ptr += "<span>Jitter percent</span>";
  ptr += "<input value=\"" + String(userSettings.jitterPercent) + "\" name=\"jitterPercent\" required placeholder=\"Enter value\" min=\"0\" max=\"100\" type=\"number\" step=\"1\"/>";
  ptr += "</div>";

  ptr += "<div>";
  ptr += "<span>Mirror mode</span>";
  ptr += "<select name=\"mirrorModeEnabled\">";
  if (userSettings.mirrorModeEnabled) {
    ptr += "<option value=\"true\" selected>Enabled</option>";
    ptr += "<option value=\"false\">Disabled</option>";
  } else {
    ptr += "<option value=\"true\">Enabled</option>";
    ptr += "<option value=\"false\" selected>Disabled</option>";
  }
  ptr += "</select>";
  ptr += "</div>";

  ptr += "<input type=\"submit\" value=\"Update\">";
  ptr += "</form>";

  ptr += "</body>";
  ptr += "</html>";
  return ptr;
}
// https://flems.io/#0=N4IgtglgJlA2CmIBcA2ALAOgMwE4A0IAzvAgMYAu8UyIGphhIBhpATgPayzIDaADHj4BdAgDMICRkh6gAdgEMwiJLQAW5MNwKl2synpoAeVQEYAfAGV45chFkBzQgAIArgAco8ylEMB6U2YAOrKG8k6qrPCiALyBIL5xZgBC8qQA1k7k7E7ENnaOfvJmTEQk8BQQulIgJkhYAEwAtHxIfCAAvnhyisq0AFaM2rr65DQdXSAKSjR0DCU6evAGKgBG7FAAnk7ATlAQhG6w8htITqIIAB4A3E59LoS2ohuNCyOnpEuUrDfysBD2skaEEoYEI70+8G+4Xg-3UpxMfD4ADdVDdzvALo09pEKrp3pwXGBZDd2lcSsQyLYqjQWgicM1Wh0hO0gA
String getUpdateUserSettingsSuccessHtml () {
  String ptr = "<!DOCTYPE html> <html>";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">";
  ptr += "<title>Pd-device settings</title>";
  ptr += "<style>";
  ptr += "body { display: flex; justify-content: center; align-items: center; height: 100vh; flex-direction: column; };";
  ptr += "</style>";
  ptr += "</head>";
  ptr += "<body>";
  ptr += "<h1>Settings updated!</h1>";
  ptr += "<a href=\"/\">Back to settings</a>";
  ptr += "</body>";
  ptr += "</html>";
  return ptr;
}

String getUpdateUserSettingsFailHtml () {
  String ptr = "<!DOCTYPE html> <html>";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">";
  ptr += "<title>Pd-device settings</title>";
  ptr += "<style>";
  ptr += "body { display: flex; justify-content: center; align-items: center; height: 100vh; flex-direction: column; };";
  ptr += "</style>";
  ptr += "</head>";
  ptr += "<body>";
  ptr += "<h1>Error</h1>";
  ptr += "<a href=\"/\">Back to settings</a>";
  ptr += "</body>";
  ptr += "</html>";
  return ptr;
}