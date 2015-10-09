#include <iostream>
#include <fstream>
#include <stdio.h>
#include <iterator>
#include <cassert>
#include <vector>
#include <inttypes.h>
#include <getopt.h>

#include <google/protobuf/compiler/importer.h> // For loading the schema file
#include <google/protobuf/descriptor.h>        // For descriptor
#include <google/protobuf/dynamic_message.h>   // For parsing from stream
#include <google/protobuf/io/zero_copy_stream_impl.h> // For io wrapper class


#define DISALLOW_COPY_AND_ASSIGN(X) \
  void operator=( const X& ); \
  X(const X&)

#define UNREACHABLE() assert(!"Unreachable")

namespace {
using namespace google::protobuf;

std::string to_string( double );
std::string to_string( float  );

class message_to_json {
public:
  // Option for the converstion
  struct option {
    bool double_to_string;
    bool float_to_string;

    // Not allow real number to be output as number digits
    // but force them as string literal there.
    void set_real_to_string() {
      double_to_string = true;
      float_to_string = true;
    }

    // For enum type, not only display the name of enum value but
    // also display the index of this enum value
    bool display_enum_index;

    option():
      double_to_string( false ),
      float_to_string( false ),
      display_enum_index( false )
    {}
  };


  message_to_json( Message* message , // Input message
                   std::ostream& output ,
                   const option& opt ):
    m_message( message ),
    m_output ( output ),
    m_option( opt )
  {}

  void convert();

private:

  // This function convert an atomic field value into a json field.
  void convert_atomic_field( const Message* message , const FieldDescriptor& field );
  void convert_nested_field( const Message* message , const Descriptor& field );
  void convert_enum_field( const Message* message , const FieldDescriptor& field );

  Message* m_message;
  std::ostream& m_output;
  option m_option;
};


void message_to_json::convert_atomic_field( const Message* message , const FieldDescriptor& field ) {
  const Reflection* reflection = message->GetReflection();

#define DO_(Type,type,OUTPUT) \
    do { \
      if( field.is_repeated() ) { \
        const int size = reflection->FieldSize(*message,&field); \
        m_output<<"\""<<field.name()<<"\":["; \
        for( int i = 0 ; i < size ; ++i ) { \
          m_output << reflection->GetRepeated##Type(*message,&field,i); \
          if( i != size - 1 ) { \
            m_output << ","; \
          } \
        } \
        m_output <<"]"; \
      } else { \
        if( reflection->HasField(*message,&field) ) { \
          m_output<<"\""<<field.name()<<"\":"; \
          type value = reflection->Get##Type(*message,&field); \
          OUTPUT(m_output,value); \
        } else { \
          m_output<<"\""<<field.name()<<"\":null"; \
        } \
      } \
    } while(0); break

#define VALUE_OUTPUT(O,V) O<<"\""<<V<<"\""

#define BOOLEAN_OUTPUT(O,V) \
  do { \
    O << (V?"true":"false"); \
  } while(0)

#define FLOAT_OUTPUT(O,V) \
  do { \
    if( m_option.float_to_string ) { \
      O << "\""<<to_string(V)<<"\""; \
    } else { \
      O << V; \
    } \
  } while(0)

#define DOUBLE_OUTPUT(O,V) \
  do { \
    if( m_option.double_to_string ) { \
      O << "\""<<to_string(V)<<"\""; \
    } else { \
      O << V; \
    } \
  } while(0)


  switch( field.cpp_type() ) {
    case FieldDescriptor::CPPTYPE_BOOL:
      DO_(Bool,bool,BOOLEAN_OUTPUT);
    case FieldDescriptor::CPPTYPE_FLOAT:
      DO_(Float,float,FLOAT_OUTPUT);
    case FieldDescriptor::CPPTYPE_DOUBLE:
      DO_(Double,double,DOUBLE_OUTPUT);
    case FieldDescriptor::CPPTYPE_INT32:
      DO_(Int32,int32_t,VALUE_OUTPUT);
    case FieldDescriptor::CPPTYPE_INT64:
      DO_(Int64,int64_t,VALUE_OUTPUT);
    case FieldDescriptor::CPPTYPE_UINT32:
      DO_(UInt32,uint32_t,VALUE_OUTPUT);
    case FieldDescriptor::CPPTYPE_UINT64:
      DO_(UInt64,uint64_t,VALUE_OUTPUT);
    case FieldDescriptor::CPPTYPE_STRING:
      DO_(String,std::string,VALUE_OUTPUT);
    default:
      UNREACHABLE();
      return;
  }
#undef DO_
#undef VALUE_OUTPUT
#undef BOOLEAN_OUTPUT
#undef FLOAT_OUTPUT
#undef DOUBLE_OUTPUT
}

void message_to_json::convert_enum_field( const Message* message , const FieldDescriptor& field ) {
  assert( field.cpp_type() == FieldDescriptor::CPPTYPE_ENUM );
  const Reflection* reflection = message->GetReflection();
  if( field.is_repeated() ) {
    const int size = reflection->FieldSize(*message,&field);
    m_output<<"\""<<field.name()<<"\":[";
    for( int i = 0 ; i < size ; ++i ) {
      const EnumValueDescriptor* enum_value =
        reflection->GetRepeatedEnum(*message,&field,i);
      if( m_option.display_enum_index ) {
        m_output<<"{\"value\":\""<<
          enum_value->name()<<"\",\"index\":"<<
          enum_value->index()<<"}";
      } else {
        m_output<<"\""<<enum_value->name()<<"\"";
      }
      if( i != size - 1 ) {
        m_output<<",";
      }
    }
    m_output<<"]";
  } else {
    if( reflection->HasField(*message,&field) ) {
      m_output<<"\""<<field.name()<<"\":";
      const EnumValueDescriptor* enum_value = reflection->GetEnum(*message,&field);
      if( m_option.display_enum_index ) {
        m_output<<"{\"value\":\""<<enum_value->name()<<"\",";
        m_output<<"\"index\":"<<enum_value->index()<<"}";
      } else {
        m_output<<"\""<<enum_value->name()<<"\"";
      }
    } else {
      m_output<<"\""<<field.name()<<"\":null";
    }
  }
}

void message_to_json::convert_nested_field( const Message* message , const Descriptor& message_descriptor ) {
  // Iterate through each field descriptors and then dispatch it
  const int size = message_descriptor.field_count();
  m_output<<"{";
  for( int i = 0 ; i < size ; ++i ) {
    const FieldDescriptor* field = message_descriptor.field(i);
    switch(field->cpp_type()) {
      case FieldDescriptor::CPPTYPE_ENUM:
        convert_enum_field(message,*field);
        break;
      case FieldDescriptor::CPPTYPE_MESSAGE:
        {
          m_output<<"\""<<field->name()<<"\":";
          const Reflection* reflection = message->GetReflection();

          // Checking if this message is repeated or just a singular one
          if( field->is_repeated() ) {
            // Dump message one by one here
            const int size = reflection->FieldSize(*message,field);
            m_output<<"[";
            for( int i = 0 ; i < size ; ++i ) {
              convert_nested_field( &(reflection->GetRepeatedMessage(
                      *message,field,i)),*(field->message_type()));
              if( i != size - 1 ) {
                m_output<<",";
              }
            }
            m_output<<"]";
          } else {
            convert_nested_field( &(reflection->GetMessage(
                    *message,field)),*(field->message_type()));
          }
          break;
        }
      default:
        convert_atomic_field(message,*field);
        break;
    }
    if( i != size - 1 ) {
      m_output<<",";
    }
  }
  m_output<<"}";
}

void message_to_json::convert() {
  return convert_nested_field(m_message,*m_message->GetDescriptor());
}

std::string to_string( float value ) {
  char buf[1024];
  sprintf(buf,"%f",value);
  return std::string(buf);
}

std::string to_string( double value ) {
  char buf[1024];
  sprintf(buf,"%f",value);
  return std::string(buf);
}

struct option kOptions[] = {
  {"proto",required_argument,0,'p'},
  {"message",required_argument,0,'m'},
  {"double_to_string",optional_argument,0,'d'},
  {"float_to_string",optional_argument,0,'f'},
  {"display_enum_index",optional_argument,0,'e'}
};

struct command_option {
  std::string proto_path;
  std::string message;
  message_to_json::option option;
};

void show_error() {
  std::cerr<<"Usage:\n";
  std::cerr<<"Convert a protocol buffer record to json format!\n";
  std::cerr<<" --proto,-p                           Protocol buffer schema file path\n";
  std::cerr<<" --message,-m                         Message name\n";
  std::cerr<<" --double_to_string,-d                Output double as string instead of numeric number\n";
  std::cerr<<" --float_to_string,-f                 Output float as string instead of numeric number\n";
  std::cerr<<" --display_enum_index,-e              Display enum value's index\n";
}

bool parse_command( int argc, char* argv[] , command_option* opt ) {
  int opt_index = 0;
  int c;
  while((c = getopt_long(argc,argv,"p:m:dfpre",kOptions,&opt_index))!=-1) {
    switch(c) {
      case 'p':
        opt->proto_path = optarg;
        break;
      case 'm':
        opt->message = optarg;
        break;
      case 'd':
        opt->option.double_to_string = true;
        break;
      case 'f':
        opt->option.float_to_string = true;
        break;
      case 'e':
        opt->option.display_enum_index = true;
        break;
      default:
        show_error();
        return false;
    }
  }
  if( opt->proto_path.empty() || opt->message.empty() ) {
    show_error();
    return false;
  }
  return true;
}

std::string read_from_stdin() {
  cin>>std::noskipws;
  return std::string( std::istream_iterator<char>(cin),
      std::istream_iterator<char>());
}

class single_file_error_collector : public compiler::MultiFileErrorCollector {
public:
  virtual void AddError( const std::string& filename,
      int line, int column, const std::string& error ) {
    std::cerr<<"Schema file failed:"<<filename
      <<" at("
      <<line<<","
      <<column<<") with message:"
      <<error<<std::endl;
  }
};

class single_file_source_tree : public compiler::SourceTree {
public:
  single_file_source_tree( const std::string& root_file ):
    compiler::SourceTree(),
    m_path_prefix(),
    m_stream_list() {
      build_path_prefix(root_file);
    }

  io::ZeroCopyInputStream* Open( const string& filename ) {
    std::ifstream* file = new std::ifstream();
    std::string p;
    if( m_path_prefix.empty() ) {
      p = filename;
    } else {
      p = m_path_prefix + "/" + filename;
    }
    file->open( p.c_str() , std::ios_base::in );
    if( !file ) {
      delete file;
      return NULL;
    } else {
      m_stream_list.push_back(file);
      return new io::IstreamInputStream(file);
    }
  }

  ~single_file_source_tree() {
    for( std::size_t i = 0 ; i < m_stream_list.size() ; ++i ) {
      delete m_stream_list[i];
    }
  }

private:
  void build_path_prefix( const std::string& root_file );
  std::string m_path_prefix;
  std::vector<std::ifstream*> m_stream_list;
};


void single_file_source_tree::build_path_prefix( const std::string& root_file ) {
  std::size_t npos = root_file.find_last_of("/");
  if( npos != std::string::npos ) {
    m_path_prefix = root_file.substr(0,npos);
  }
}


void get_filename( const std::string& fn_with_path , std::string* filename ) {
  std::size_t ipos = fn_with_path.find_last_of("/");
  if( ipos == std::string::npos ) {
    filename->assign(fn_with_path);
  } else {
    filename->assign(fn_with_path.substr(ipos+1,fn_with_path.size()-ipos-1));
  }
}
} // namespace


int main( int argc, char* argv[] ) {
  command_option opt;
  if( !parse_command(argc,argv,&opt) )
    return -1;

  single_file_source_tree source_tree( opt.proto_path );
  single_file_error_collector err_coll;
  std::string filename;
  get_filename(opt.proto_path,&filename);

  // Now read in the proto schema file
  compiler::Importer importer( &source_tree , &err_coll );

  if( importer.Import(filename) == NULL ) {
    return -1;
  }
  const DescriptorPool* pool = importer.pool();

  // Get the descriptor we need
  const Descriptor* desp = pool->FindMessageTypeByName(opt.message);
  if( desp == NULL ) {
    std::cerr<<"Cannot find message type in schema file:"
      <<opt.message<<std::endl;
    return -1;
  }

  DynamicMessageFactory factory(pool);
  const Message* message = factory.GetPrototype(desp);
  if( message == NULL ) {
    std::cerr<<"Cannot get default message for message name:"
      <<opt.message<<std::endl;
    return -1;
  }

  // Now readin the data stream
  std::string data = read_from_stdin();
  Message* mutable_message = message->New();

  if( !mutable_message->ParseFromString(data) ) {
    std::cerr<<"Cannot parse the input stream!";
    return -1;
  }

  message_to_json conv(mutable_message,std::cout,opt.option);
  conv.convert();

  std::cout.flush();
  return 0;
}
