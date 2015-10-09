Proto2Json
===================
A tool that can dump ARBITARY protocol data into json.

#1. Motivation
Protocol buffer is built for code generation, this means everytime you modify your protocols, you need  to recompile.
However some stuff related to protocol buffer may not be valuable to recompile everytime, like dumping
protocol buffer into json. The proto2json binary allows you to dump an arbitary protocol buffer data into
a json format as long as you have the .proto file.

#2. How to
```cat some_protobuf_data | proto2json --proto my_proto.proto --message some.namespace.ClassName | json_pp```

In order to invoke proto2json, you need to tell me the protobuf scheme file and also tell me which message
you want to dump. That's it. Then you just cat the data into proto2json it will generate valid json for you.

#3. Notes
Only support protocol buffer version 2 now.
