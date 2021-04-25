pio=pipenv run platformio --

build:
	${pio} run

upload:
	${pio} run -t upload

uploadfs:
	${pio} run -t uploadfs

erase:
	${pio} run -t erase

monitor:
	${pio} device monitor

dependencies:
	pipenv install
	${pio} lib install

devapi:
	pipenv run python tools/server.py
