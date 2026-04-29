function(load_dotenv DOTENV_FILE)
    if(NOT EXISTS "${DOTENV_FILE}")
        message(FATAL_ERROR ".env file not found: ${DOTENV_FILE}")
    endif()

    file(READ "${DOTENV_FILE}" ENV_FILE_CONTENT)
    string(REGEX MATCHALL "[^\n]*\n?" ENV_LINES "${ENV_FILE_CONTENT}")

    foreach(line ${ENV_LINES})
        string(STRIP "${line}" line)

        # 跳过空行和注释
        if(line MATCHES "^[ \t]*$" OR line MATCHES "^[ \t]*#")
            continue()
        endif()

        # 匹配 KEY=VALUE
        if(line MATCHES "^([^=]+)=(.*)$")
            set(key "${CMAKE_MATCH_1}")
            set(value "${CMAKE_MATCH_2}")

            string(STRIP "${key}" key)
            string(STRIP "${value}" value)

            # 去掉简单引号/双引号包裹
            if(value MATCHES "^\"(.*)\"$")
                set(value "${CMAKE_MATCH_1}")
            elseif(value MATCHES "^'(.*)'$")
                set(value "${CMAKE_MATCH_1}")
            endif()

            # 写到调用方作用域
            set(${key} "${value}" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()