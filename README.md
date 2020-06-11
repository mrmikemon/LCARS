# LCARS - Enterprise Retrofit Electronics

This project contains is the files for building the Enterprise Retrofit model electronics and control software for lighting.

The name LCARS comes from the Star Trek computer system - Library Computer Access/Retrieval System (LCARS).

The electronics circuit diagram provided is for an Ardunino Nano microcontroller using a DFPlayer mini for sound.

## Features

The features of the circuit are:

* Cabin lighting curcuit - intended for LED Strips within the model
* Spotlights - a curcuit for illuminating the body of the ship and the famous NCC-1701 ship badge.
* Navigation Lights - a circuit to control green, red and other navigation lights
* Strobes - a strobe light circuit for various strobes around the model (as per the movies)
* Warp Engine - a circuit for warp engines. Intended to drive LED strips in the warp engines
* Dish & Crystal lighting - there are 3 RGB circuits for different colour shifts from impulse to warp etc.
* Photon Torpedoes - 2 circuits for left and right photon torpedo tube LEDS
* Thrusters - a single circuit to control thrusters around the dish and the top dish of the ship
* Serial interface to sound card like DFPlayer mini
* 3 push buttons for modes such as demo, photon torpedos & warp mode

## Contents

Contents of the repo:

* `LCARS-v1.drawio-11.xml` - circuit schematic diagram for the various parts of the model
* `LCARS.ino` - the Ardino sketch source code 'C' with control for various animations, flashing lights and button control.

## Work still to be done

Well there are a few things:

* Improve the introduction (start up of the model). Want to get the lighting of the model to start up step by step synced with the introduction sound.
* Preferences have not been implemented - although there is a 'sounds' directory with sounds for the preferences. The software does allow you to enter and exit a preferences mode - none of the settings work however.
* Build a demo mode to run through various animations sequences etc.
