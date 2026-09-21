target_sources(app PRIVATE
  hmi_actor.c
  hmi_mcumgr.c
)

target_include_directories(app PRIVATE
  include
)
