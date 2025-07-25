add_executable(live_trading_engine main.cpp)

target_link_libraries(live_trading_engine 
    PRIVATE 
    trading_engine
    Threads::Threads
) 