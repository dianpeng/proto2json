all: src/proto2json.cc
	g++ -O2 src/proto2json.cc -lprotobuf -o proto2json

.PHONY:clean
clean:
	rm proto2json
