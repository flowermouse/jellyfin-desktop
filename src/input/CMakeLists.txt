set(INPUT_SRCS
  InputComponent.cpp
  InputComponent.h
  InputMapping.cpp
  InputMapping.h
  InputKeyboard.h
  InputSocket.h
  InputSocket.cpp
)

add_subdirectory(apple)

if(SDL2_FOUND)
  list(APPEND INPUT_SRCS InputSDL.cpp InputSDL.h)
endif(SDL2_FOUND)

add_sources(${INPUT_SRCS})

