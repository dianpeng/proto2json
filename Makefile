all: src/proto2json.cc src/base64.h src/base64.cc
	g++ -O2 src/proto2json.cc src/base64.cc -lprotobuf -o proto2json

.PHONY:clean
clean:
	rm proto2json
