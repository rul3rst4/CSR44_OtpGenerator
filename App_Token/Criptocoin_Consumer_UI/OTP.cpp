// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include <Windows.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GL/glew.h>
#include <GLFW/glfw3.h> // Will drag system OpenGL headers
#include <iostream>
#include <librdkafka/rdkafkacpp.h>
#include <optional>
#include <format>
#include <nlohmann/json.hpp>
#include <fstream>
#include <openssl/evp.h>
#include <filesystem>
#include <chrono>
#include <curl/curl.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif
#include <future>

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

//// Simple helper function to load an image into a OpenGL texture with common settings
bool LoadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height)
{
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

std::vector<unsigned char> decode_base64(std::string& hash) {
    std::vector<unsigned char> hash_array{};
    hash_array.resize(hash.size() * 2);
    int decoded_len = EVP_DecodeBlock(hash_array.data(), reinterpret_cast<unsigned char*>(hash.data()), hash.size());
    std::cout << "Decoded Size: " << decoded_len << std::endl;
    hash_array.resize(decoded_len);

    return hash_array;
}

std::string sha256(const std::string str) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);

    EVP_DigestUpdate(mdctx, str.c_str(), strlen(str.c_str()));
    EVP_DigestFinal_ex(mdctx, hash, &hash_len); 
    EVP_MD_CTX_free(mdctx);

    int output_len = 4 * ((hash_len + 2) / 3);
    std::string output(output_len, '\0');
    int encoded_len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()), hash, hash_len);
    std::cout << "String codificada: " << output << std::endl;

    return output;
}

void create_form_field(std::string_view label, std::string& value) {
    ImGui::Text(label.data());
    //ImGui::SameLine();
    ImGui::InputText(std::format("##{}", label).c_str(), value.data(), value.size());
}


std::string generate_otp(std::vector<unsigned char> salt, std::vector<unsigned char> seed_passwd, std::tm time) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);

    auto datetime = std::string{ std::to_string(time.tm_year) }
        .append(std::to_string(time.tm_mon))
        .append(std::to_string(time.tm_mday))
        .append(std::to_string(time.tm_hour))
        .append(std::to_string(time.tm_min))
        .append(std::to_string(time.tm_sec));

    EVP_DigestUpdate(mdctx, salt.data(), salt.size() - 1);
    EVP_DigestUpdate(mdctx, seed_passwd.data(), seed_passwd.size() - 1);
    EVP_DigestUpdate(mdctx, datetime.data(), datetime.size());
    EVP_DigestFinal_ex(mdctx, hash, &hash_len);
    EVP_MD_CTX_free(mdctx);

    std::string otp{};
    for (auto& c : hash) {
        otp.append(std::to_string(c));
    }

    otp.resize(6);

    return otp;
}

nlohmann::json get_user_data() {
    std::ifstream acess_file("acess.json");
    nlohmann::json json_data;
    acess_file >> json_data;

    return json_data;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::vector<uint8_t> get_salt_qrcode(std::string& username) {
    CURL* curl = curl_easy_init();
    CURLcode res;

    nlohmann::json user_data;
    user_data["username"] = username.c_str();

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:32768/GetSalt");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    struct curl_slist* headers = NULL;
    headers =curl_slist_append(headers, "accept: */*");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    auto json_user_data = user_data.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_user_data.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    /* always cleanup */
    curl_easy_cleanup(curl);

    auto response = nlohmann::json::parse(readBuffer);
    auto salt_qrcode = response["randomSalt"].get<std::string>();
    auto decoded_qrcode = decode_base64(salt_qrcode);

    std::ofstream f("./output.png", std::ios::binary);

    for(const auto& c : decoded_qrcode)
                f << c;
    f.close();

    return decoded_qrcode;
}

std::tm get_datetime_now() {
    CURL* curl = curl_easy_init();
    CURLcode res;

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, "http://worldtimeapi.org/api/timezone/America/Sao_Paulo");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

    /* always cleanup */
    curl_easy_cleanup(curl);

    auto datetime_now = nlohmann::json::parse(readBuffer);

    std::tm t = {};
    std::istringstream ss(datetime_now["datetime"].get<std::string>());
    ss >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");
    t.tm_year += 1900;
    t.tm_mon += 1;

    return t;
}

bool create_user(nlohmann::json user_data) {
    CURL* curl = curl_easy_init();
    CURLcode res;

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:32768/CreateUser");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "accept: */*");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    auto json_user_data = user_data.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_user_data.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                       curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 201) {
        return false;
    }

    /* always cleanup */
    curl_easy_cleanup(curl);
    
    return true;
}

bool GetSecuredContent(nlohmann::json user_data) {
    CURL* curl = curl_easy_init();
    CURLcode res;

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:32768/GetSecuredContent");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "accept: */*");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    auto json_user_data = user_data.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_user_data.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        return false;
    }

    /* always cleanup */
    curl_easy_cleanup(curl);

    return true;
}

bool login(nlohmann::json user_data) {
    CURL* curl = curl_easy_init();
    CURLcode res;

    std::string readBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:32768/Login");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "accept: */*");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    auto json_user_data = user_data.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_user_data.c_str());
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        return false;
    }

    /* always cleanup */
    curl_easy_cleanup(curl);

    return true;
}

void OTP() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    auto center = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x / 2 - 200, viewport->WorkPos.y + viewport->WorkSize.y / 2 - 200);
    ImGui::SetNextWindowPos(center);
    ImGui::SetNextWindowSize(ImVec2(400, 550));
    ImGui::SetNextWindowViewport(viewport->ID);

    static bool authenticated = false;

    if (!std::filesystem::exists("acess.json")) {
        ImGui::Begin("OTP (First Access)", nullptr, 0);
        static std::string username(100, '\0');
        static std::string passwd(100, '\0');
        static std::string passwd_seed(100, '\0');
        static std::string salt(100, '\0');

        static int my_image_width = 0;
        static int my_image_height = 0;
        static GLuint my_image_texture = 0;

        create_form_field("Username:", username);
        create_form_field("Password(Local):", passwd);
        create_form_field("Password(Seed):", passwd_seed);
        auto base64_seed_passwd = sha256(passwd_seed);
        ImGui::Text("Hash: %s", base64_seed_passwd.c_str());
        create_form_field("Salt:", salt);

        static bool invalid_data = false;
        static bool passwd_seed_equal = false;

        if (ImGui::Button("Get Salt")) {

            auto qrcode = get_salt_qrcode(username);

            bool ret = LoadTextureFromFile("./output.png", &my_image_texture, &my_image_width, &my_image_height);
        }

        if (ImGui::Button("Generate")) {
            if (username[0] == '\0' || passwd[0] == '\0' || passwd_seed[0] == '\0' || salt[0] == '\0') {
                invalid_data = true;
            }
            else if(passwd == passwd_seed) {
                invalid_data = true;
                passwd_seed_equal = true;
            }
            else {
                invalid_data = false;
                nlohmann::json json;
                auto base64_salt = sha256(salt);
                auto base64_local_passwd = sha256(passwd);
                json["salt"] = base64_salt.c_str();
                json["username"] = username.c_str();
                json["local_password"] = base64_local_passwd.c_str();
                json["seedpassword"] = base64_seed_passwd.c_str();

                if (create_user(json)) {
                    std::string data = json.dump();
                    std::ofstream output("acess.json");
                    output << data;
                    output.close();
                }
            }
        }

        if (invalid_data) {
            ImGui::Text("Invalid data");
        }

        if(passwd_seed_equal) {
            ImGui::Text("Password(Seed) and Password(Local) can't be equal");
        }

        ImGui::End();

        if (my_image_texture != 0) {
            ImGui::Begin("Salt QRCODE");
            ImGui::Text("pointer = %p", my_image_texture);
            ImGui::Text("size = %d x %d", my_image_width, my_image_height);
            ImGui::Image((void*)(intptr_t)my_image_texture, ImVec2(my_image_width/2.0f, my_image_height/2.0f));
            ImGui::End();
        }

    }
    else if (!authenticated) {
        ImGui::Begin("OTP (Authentication)");

        static std::string username(100, '\0');
        static std::string passwd(100, '\0');

        create_form_field("Username:", username);
        create_form_field("Password(Local):", passwd);

        static bool invalid_passwd = false;
        if (ImGui::Button("Login")) {
            std::ifstream acess_file("acess.json");
            nlohmann::json json_data = get_user_data();

            auto j_username = json_data["username"].get<std::string>();
            auto j_passwd = json_data["local_password"].get<std::string>();

            auto try_passwd = sha256(passwd);
            if (strcmp(j_username.c_str(), username.c_str()) != 0 || strcmp(try_passwd.c_str(), j_passwd.c_str()) != 0) {
                invalid_passwd = true;
            }
            else {
                authenticated = true;
            }
        }

        if (invalid_passwd) {
            ImGui::Text("Invalid username or password. Try again.");
        }

        ImGui::End();
    }
    else {
        ImGui::Begin("OTP");
        static std::string otp{};
        static int is_valid_otp{-1};

        if (ImGui::Button("Create OTP")) {
            auto user_data = get_user_data();

            auto b64_salt = user_data["salt"].get<std::string>();
            auto b64_local_passwd = user_data["seedpassword"].get<std::string>();

            auto salt = decode_base64(b64_salt);
            auto local_passwd = decode_base64(b64_local_passwd);

            auto datetime_now = get_datetime_now();

            otp = generate_otp(salt, local_passwd, datetime_now);
        }

        if (ImGui::Button("Validate OTP")) {
            auto user_data = get_user_data();
            nlohmann::json validate_data;
            validate_data["OtpString"] = otp;
            validate_data["Username"] = user_data["username"];

            is_valid_otp = GetSecuredContent(validate_data);
        }

        if (is_valid_otp == 1) {
            ImGui::Text("OTP is valid");
        }
        else if(is_valid_otp == 0) {
            ImGui::Text("OTP is invalid");
        }
        
        ImGui::Text("Current OTP: %s", otp.c_str());

        ImGui::End();
    }
}

// Main code
int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1920, 1080, "Dear ImGui GLFW+OpenGL3+CG", NULL, NULL);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    glewExperimental = true;
    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        std::cerr << "Error: " << glewGetErrorString(err) << std::endl;
        glfwTerminate();
        return -1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    //InitConsumer();

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See Makefile.emscripten for details.
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // Our state
    bool show_demo_window = true;
    auto clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = NULL;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    while (!glfwWindowShouldClose(window))
#endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        //// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        //if (show_demo_window)
            //ImGui::ShowDemoWindow(&show_demo_window);

        //static GLuint zbuffer_bit = 0;
        //// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        //{
        //    ImGui::Begin("Info");                          // Create a window called "Hello, world!" and append into it.

        //    ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state

        //    ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color
        //    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        //    ImGui::Text("OpenGL Version: %s", glGetString(GL_VERSION));

        //    ImGui::End();
        //}

        OTP();

        // Rendering
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
