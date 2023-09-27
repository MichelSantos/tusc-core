file(GLOB HEADERS "include/graphene/nft_history/*.hpp")

add_library( graphene_nft_history
        nft_history.cpp
           )

target_link_libraries( graphene_nft_history graphene_chain graphene_app )
target_include_directories( graphene_nft_history
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

if(MSVC)
  set_source_files_properties(nft_history.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

install( TARGETS
   graphene_nft_history

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
INSTALL( FILES ${HEADERS} DESTINATION "include/graphene/nft_history" )
