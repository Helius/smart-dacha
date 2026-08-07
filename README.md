### Notes
#### Top tank sensor
- esp32c3 with ext antenna
- uses wifi to publish self state to a mqtt broker
- no battery no outputs
- has an ultrasonic sensor and also a floating switch

#### Low tank unit
- esp32c3 with ext antenna
- reads ultrasonic sensor and controls a servo valve with sensors
- powered solely from battery (need to be optimized, or be solar powered)
- use esp-now to report self state to esp-now hub (see `valve & pump unit`)
  
#### Valve & pump unit
- esp32c3 with ext antenna
- uses wifi to publish self state and the low tank unit state to the mqtt broker
- serves also as esp-now hub for the low tank unit
- controls a servo valve with sensors and pump
