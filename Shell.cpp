#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <iostream>
#include <cassert>
#include <iostream>
namespace fs = std::filesystem;
/* класс Shell, представляющий собой среду для выполнения некоторых команд
 * над файловой системой. По аналогии с настоящим шеллом он поддерживает несколько команд:
 * - ls [directory] -- вывести содержимое указанной директории. Если директория не указана, то используется текущая директория (current working directory, cwd)
 * - cat <file> -- вывести содержимое файла
 * - mkdir <directory> -- создать директорию
 * - rmdir <directory> -- удалить пустую директорию
 * - rm <file> -- удалить файл
 * - cd <directory> -- сделать <directory> текущей директорией
 * - echo [text] -- вывести текст
 *
 * Если в конце строки указать "> <file>", то результат выполнения команды будет записан в файл <file>, при этом старое содержимое файла будет удалено.
 * Если в конце строки указать ">> <file>", то результат выполнения команды будет записан в конец файла <file>.
 *
 * Все операции должны производиться с настоящей файловой системой.
 * В случае возникновения ошибок во время выполнения команды шелл должен вернуть код ответа 1, в случае успеха вернуть 0.
 * Также на оценку влияет потенциальная расширяемость набора команд.
 */
class Shell {
public:
    Shell(const std::filesystem::path& cwd_) {cwd = fs::current_path();}
    /*
     * Выполнить команду.
     * Команда подается в виде строки. Вывод команды попадает в поток `out`.
     * Если в команде вывод был перенаправлен в файл с помощью `>` или `>>`, то в `out` ничего не выводится.
     * Если команда была выполнена, то метод должен вернуть 0, иначе 1.
     * Перед выполнением команды рекомендуем вывести в поток `out` строку вида "$ <строка команды>\n", чтобы
     * во время отладки было понятно, какие ответы соответствуют каким введенным командам.
     */
    int ExecuteCommand(const std::string& command, std::ostream& out) {
        out << "$ " << command << '\n';

        std::istringstream iss(command);
        std::vector<std::string> args;
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }
        
        if (args.empty()) return 1;

        std::string cmd = args[0];
        std::string output_file;
        bool append = false, written = false;

        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == ">" || args[i] == ">>") {
                if (i + 1 < args.size()) {
                    output_file = args[i + 1];
                    append = (args[i] == ">>");
                    args.resize(i);
                    break;
                } else {
                    return 1;
                }
            }
        }

        std::ostringstream temp_out;

        int result = 1;
        
        if (cmd == "ls") {
            result = ls(args, output_file.empty() ? out : temp_out);
        } else if (cmd == "cat") {
            result = cat(args, out, output_file, append);
            written = true;
        } else if (cmd == "mkdir") {
            result = mkdir(args);
        } else if (cmd == "rmdir") {
            result = rmdir(args);
        } else if (cmd == "rm") {
            result = rm(args);
        } else if (cmd == "cd") {
            result = cd(args);
        } else if (cmd == "echo") {
            result = echo(args, output_file.empty() ? out : temp_out);
        }
        if (!output_file.empty() && !written) {
            std::ofstream file(cwd / output_file, append ? std::ios::app : std::ios::trunc);
            if (!file.is_open()) return 1;
            file << temp_out.str();
        }
    
        return result;
    }

private:
    fs::path cwd;

    int ls(const std::vector<std::string>& args, std::ostream& out ) {
        std::filesystem::path dir = (args.size() > 1) ? fs::path(args[1]) : cwd;
        if (!std::filesystem::exists(dir)) return 1;
        
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            out << entry.path().filename().string() << "\n";
        }
        return 0;
    }

    int cat(const std::vector<std::string>& args, std::ostream& out, const std::string& output_file, bool append) {
        if (args.size() < 2) return 1;
        std::ifstream from(cwd/args[1]);
        if (!from.is_open()) return 1;
        if (!output_file.empty()) {
            std::ofstream to(cwd/output_file, append ? std::ios::app : std::ios::trunc);
            to << from.rdbuf();
        }
        else {
            out << from.rdbuf();
        }
        return 0;
    }

    int mkdir(const std::vector<std::string>& args) {
        if (args.size() < 2) return 1;
        return std::filesystem::create_directory(cwd/args[1]) ? 0 : 1;
    }

    int rmdir(const std::vector<std::string>& args) {
        if (args.size() < 2) return 1;
        return std::filesystem::remove_all(cwd/args[1]) ? 0 : 1;
    }

    int rm(const std::vector<std::string>& args) {
        if (args.size() < 2) return 1;
        return std::filesystem::remove(cwd/args[1]) ? 0 : 1;
    }

    int cd(const std::vector<std::string>& args) {
        if (args.size() < 2) return 1;
        fs::path currentPath = cwd/args[1];
        if (std::filesystem::exists(currentPath) && std::filesystem::is_directory(currentPath)) {
            cwd = currentPath;
            return 0;
        }
        
        return 1;
    }

    int echo(const std::vector<std::string>& args, std::ostream& out) {
        for (size_t i = 1; i < args.size(); ++i) {
            out << args[i] << " ";
        }
        out << "\n";
        return 0;
    }
};

int main() {

    Shell shell(std::filesystem::temp_directory_path());
    // shell.ExecuteCommand("rmdir test_solution_1234", std::cout);
    
    assert(shell.ExecuteCommand("mkdir test_solution_1234", std::cout) == 0);
    assert(shell.ExecuteCommand("ls", std::cout) == 0);
    assert(shell.ExecuteCommand("cd test_solution_1234", std::cout) == 0);
    assert(shell.ExecuteCommand("echo Hello, World! > test.txt", std::cout) == 0);
    assert(shell.ExecuteCommand("cat test.txt", std::cout) == 0);
    assert(shell.ExecuteCommand("cat test.txt > test2.txt", std::cout) == 0);
    assert(shell.ExecuteCommand("echo Goodbye >> test2.txt", std::cout) == 0);
    assert(shell.ExecuteCommand("cat test2.txt", std::cout) == 0);
    assert(shell.ExecuteCommand("ls", std::cout) == 0);
    assert(shell.ExecuteCommand("ls ../test_solution_1234", std::cout));
    assert(shell.ExecuteCommand("rm test.txt", std::cout) == 0);
    assert(shell.ExecuteCommand("rm test2.txt", std::cout) == 0);
    assert(shell.ExecuteCommand("ls", std::cout) == 0);
    assert(shell.ExecuteCommand("cd ..", std::cout) == 0);
    assert(shell.ExecuteCommand("rmdir test_solution_1234", std::cout) == 0);

    assert(shell.ExecuteCommand("rmdir test_solution_1234", std::cout) == 1);

    assert(shell.ExecuteCommand("cd test_solution_1234", std::cout) == 1);
}
