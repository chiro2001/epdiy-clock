#!/bin/bash

PROJECT_DIR=$(cd `dirname $0`/..; pwd)

python $PROJECT_DIR/components/epdiy/scripts/fontconvert.py \
  TimeTraveler 160 $PROJECT_DIR/res/font/TimeTravelerPal-Normal-Regular.ttf \
  time_traveler \
  --string "1234567890+-,.:AMPamp" --compress --path $PROJECT_DIR/main/font