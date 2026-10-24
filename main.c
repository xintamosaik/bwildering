#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// --- Tiny Custom Math Backend (id Software style) ---
typedef float mat4[16];

void mat4_identity(mat4 dest) {
    for (int i = 0; i < 16; i++) dest[i] = 0.0f;
    dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
}

void mat4_perspective(float fov_radians, float aspect, float near, float far, mat4 dest) {
    for (int i = 0; i < 16; i++) dest[i] = 0.0f;
    float tanHalfFovy = tanf(fov_radians / 2.0f);
    
    dest[0]  = 1.0f / (aspect * tanHalfFovy);
    dest[5]  = 1.0f / tanHalfFovy;
    dest[10] = -(far + near) / (far - near);
    dest[11] = -1.0f;
    dest[14] = -(2.0f * far * near) / (far - near);
}

void mat4_rotate(mat4 dest, float angle, float x, float y, float z) {
    float c = cosf(angle);
    float s = sinf(angle);
    float len = sqrtf(x*x + y*y + z*z);
    x /= len; y /= len; z /= len;
    float ic = 1.0f - c;

    mat4_identity(dest);
    dest[0] = x*x*ic + c;     dest[4] = x*y*ic - z*s;   dest[8]  = x*z*ic + y*s;
    dest[1] = y*x*ic + z*s;   dest[5] = y*y*ic + c;     dest[9]  = y*z*ic - x*s;
    dest[2] = x*z*ic - y*s;   dest[6] = y*z*ic + x*s;   dest[10] = z*z*ic + c;
}

void mat4_multiply(mat4 a, mat4 b, mat4 dest) {
    mat4 res;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            res[j * 4 + i] = 
                a[0 * 4 + i] * b[j * 4 + 0] +
                a[1 * 4 + i] * b[j * 4 + 1] +
                a[2 * 4 + i] * b[j * 4 + 2] +
                a[3 * 4 + i] * b[j * 4 + 3];
        }
    }
    for (int i = 0; i < 16; i++) dest[i] = res[i];
}
// -----------------------------------------------------

const char* vertexShaderSource = 
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aColor;\n"
    "out vec3 ourColor;\n"
    "uniform mat4 mvp;\n"
    "void main() {\n"
    "    gl_Position = mvp * vec4(aPos, 1.0);\n"
    "    ourColor = aColor;\n"
    "}\0";

// Simple Fragment Shader: outputs interpolated vertex colors
const char* fragmentShaderSource = 
    "#version 330 core\n"
    "in vec3 ourColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vec4(ourColor, 1.0);\n"
    "}\0";

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window; // Silence unused parameter warning
    glViewport(0, 0, width, height);
}

int main(void) {
    // 1. Initialize GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    // Configure OpenGL version 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 2. Create Window
    GLFWwindow* window = glfwCreateWindow(800, 600, "bwildering 0.1", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // 3. Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW: %s\n", glewGetErrorString(err));
        // Note: GLEW often throws an error on core profiles due to glGetString, 
        // but function pointers are usually still loaded.
    }

    // Enable depth testing for 3D rendering
    glEnable(GL_DEPTH_TEST);

    // Shaders
    GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vShader);

    GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fShader);

    GLuint program = glCreateProgram();
    glAttachShader(program, vShader);
    glAttachShader(program, fShader);
    glLinkProgram(program);
    glDeleteShader(vShader);
    glDeleteShader(fShader);

    // 5. Define Cube Vertex Data (Positions + Colors)
    float vertices[] = {
        // Positions         // Colors
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 1.0f
    };

    unsigned int indices[] = {
        0, 1, 2,  2, 3, 0, // Back
        4, 5, 6,  6, 7, 4, // Front
        0, 4, 7,  7, 3, 0, // Left
        1, 5, 6,  6, 2, 1, // Right
        3, 2, 6,  6, 7, 3, // Top
        0, 1, 5,  5, 4, 0  // Bottom
    };

    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    GLint mvpLoc = glGetUniformLocation(program, "mvp");

    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        // Build matrices
        mat4 proj, model, mvp;
        mat4_perspective(3.14159f / 4.0f, 800.0f / 600.0f, 0.1f, 100.0f, proj);
        
        // Push the cube back by Z = -3.0 (acting as our view translation)
        mat4_identity(model);
        model[14] = -3.0f; 

        // Rotate it over time
        mat4 rot;
        mat4_rotate(rot, (float)glfwGetTime(), 0.5f, 1.0f, 0.0f);
        
        // Combine into MVP: Projection * Model * Rotation
        mat4 temp;
        mat4_multiply(model, rot, temp);
        mat4_multiply(proj, temp, mvp);

        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, (float*)mvp);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(program);
    glfwTerminate();
    return 0;
}
