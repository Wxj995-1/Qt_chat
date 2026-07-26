#定义所有源文件
aux_source_directory(. SRC_LISTS)
aux_source_directory(../asynclog  LOG_LISTS)
aux_source_directory(./db DB_LISTS)
aux_source_directory(./model MODEL_LISTS)
aux_source_directory(../config CONFIG_LISTS)
aux_source_directory(./redis redis_LISTS)
#list(APPEND SRC_LISTS ../asynclog/AsyncLog.cpp ../asynclog/Platform.cpp)
#指定生成可执行文件
add_executable(ChatServer ${SRC_LISTS} ${LOG_LISTS} ${CONFIG_LISTS} ${DB_LISTS} ${MODEL_LISTS} ${redis_LISTS})


#指定可执行文件需要连接的库文件
target_link_libraries(ChatServer muduo_net muduo_base mysqlclient hiredis pthread)



