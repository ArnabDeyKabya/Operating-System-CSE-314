#!/bin/bash

# This Function to parse the input file
parse_input_file() {
    use_archive=$(sed -n '1p' "$1")
    allowed_archives=$(sed -n '2p' "$1")
    allowed_languages=$(sed -n '3p' "$1")
    total_marks=$(sed -n '4p' "$1")
    penalty_unmatched_output=$(sed -n '5p' "$1")
    working_directory=$(sed -n '6p' "$1")
    student_id_range=$(sed -n '7p' "$1")
    expected_output_file=$(sed -n '8p' "$1")
    penalty_submission_guideline=$(sed -n '9p' "$1")
    plagiarism_file=$(sed -n '10p' "$1")
    plagiarism_penalty=$(sed -n '11p' "$1")

    # Convert "python" to "py" in allowed_languages fo the input file for general purpose 
    allowed_languages=$(echo "$allowed_languages" | sed 's/python/py/g')

    if [ -z "$use_archive" ] || [ -z "$allowed_archives" ] || [ -z "$allowed_languages" ] ||
       [ -z "$total_marks" ] || [ -z "$penalty_unmatched_output" ] || [ -z "$working_directory" ] ||
       [ -z "$student_id_range" ] || [ -z "$expected_output_file" ] || [ -z "$penalty_submission_guideline" ] ||
       [ -z "$plagiarism_file" ] || [ -z "$plagiarism_penalty" ]; then
        echo "Invalid input file format."
        exit 1
    fi
}

# Function to check if a submission has plagiarism
check_plagiarism() {
    student_id=$1
    if grep -q "$student_id" "$plagiarism_file"; then
        echo "true"
    else
        echo "false"
    fi
}

# Function to check if student ID is within the valid range
is_student_id_in_range() {
    student_id=$1
    min_student_id=$(echo "$student_id_range" | cut -d' ' -f1)
    max_student_id=$(echo "$student_id_range" | cut -d' ' -f2)

    if [[ "$student_id" -ge "$min_student_id" && "$student_id" -le "$max_student_id" ]]; then
        echo "true"
    else
        echo "false"
    fi
}



# Function to validate student ID format
validate_student_id() {
    student_id=$1
    if [[ ! "$student_id" =~ ^[0-9]{7}$ ]]; then
        echo "false"
    else
        echo "true"
    fi
}

# Function to compare output file with expected output file line by line
compare_outputs_line_by_line() {
    marks=$total_marks  
    while IFS= read -r expected_line; do
        if ! grep -qF -- "$expected_line" "$output_file"; then
            marks=$((marks - penalty_unmatched_output))
        fi
    done < "$expected_output_file"
}

# Function to process a student's submission
process_submission() {
    student_id=$1
    file_path="$2"
    marks=$total_marks
    remarks=""

    if [ "$use_archive" == "true" ] && [[ -d "$file_path" ]]; then
        remarks="issue case #1: submission is a folder"
        continue
    fi

    if [ "$use_archive" == "false" ] && [[ -d "$file_path" ]]; then
        remarks="issue case #1: submission is a folder"
        continue
    fi

    # Check for valid student ID in the filename
    if [[ "$(basename "$file_path" | cut -d. -f1)" != "$student_id" ]]; then
        remarks="issue case #1 (Invalid file name)"
        echo "$student_id,0,0,0,$remarks" >> marks.csv
        mv "$file_path" issues/
        return
    fi

    # Unarchive if necessary
    #if [ "$use_archive" == "true" ]; then
        file_extension="${file_path##*.}"
        if [[ ! " ${allowed_archives[@]} " =~ " ${file_extension} " && ! -d "$file_path" && "$use_archive" == "true" ]]; then
            remarks="issue case #2 (Invalid archive format)"
            echo "$student_id,0,0,0,$remarks" >> marks.csv
            mv "$file_path" issues/
            return
        fi

        # Unzip based on the format
        if [ "$file_extension" == "zip" ]; then
            unzip "$file_path" -d "$working_directory/$student_id"
        elif [ "$file_extension" == "rar" ]; then
            unrar x "$file_path" 
            mv "$student_id" "$working_directory/$student_id"
        elif [ "$file_extension" == "tar" ]; then
            tar -xvf "$file_path" 
            mv "$student_id" "$working_directory/$student_id"
        fi

        # Check if unarchiving created a folder named after the student_id
        if [ -d "$working_directory/$student_id/$student_id" ]; then
            mv "$working_directory/$student_id/$student_id/"* "$working_directory/$student_id/"
            rmdir "$working_directory/$student_id/$student_id"
        fi
    #else
        # If not archived, ensure it is moved to the student's folder
        mkdir -p "$working_directory/$student_id"
        mv "$file_path" "$working_directory/$student_id/"
    #fi

    # Check language and ID
    found_valid_file=false
    for file in "$working_directory/$student_id"/*; do
        if [[ " ${allowed_languages[@]} " =~ " ${file##*.} " ]] && [[ "$(basename "$file" | cut -d. -f1)" == "$student_id" ]]; then
            found_valid_file=true
            output_file="$working_directory/$student_id/${student_id}_output.txt"
            if [[ "${file##*.}" == "sh" ]]; then
                bash "$file" > "$output_file"
            elif [[ "${file##*.}" == "c" ]]; then
                gcc "$file" -o "$working_directory/$student_id/${student_id}.out"
                if [ -f "$working_directory/$student_id/${student_id}.out" ]; then
                    "$working_directory/$student_id/${student_id}.out" > "$output_file"
                else
                    remarks="Compilation failed"
                    echo "$student_id,0,0,0,$remarks" >> marks.csv
                    mv "$working_directory/$student_id" issues/
                    return
                fi
            elif [[ "${file##*.}" == "cpp" ]]; then
                g++ "$file" -o "$working_directory/$student_id/${student_id}.out"
                if [ -f "$working_directory/$student_id/${student_id}.out" ]; then
                    "$working_directory/$student_id/${student_id}.out" > "$output_file"
                else
                    remarks="Compilation failed"
                    echo "$student_id,0,0,0,$remarks" >> marks.csv
                    mv "$working_directory/$student_id" issues/
                    return
                fi
            elif [[ "${file##*.}" == "python" || "${file##*.}" == "py" ]]; then
                python3 "$file" > "$output_file"
            fi
            break
        fi
    done

    if [ "$found_valid_file" == "false" ]; then
        remarks="issue case #3 (No valid files found)"
        echo "$student_id,0,0,0,$remarks" >> marks.csv
        mv "$working_directory/$student_id" issues/
        return
    fi

    # Compare output with expected output
    compare_outputs_line_by_line

    # Handle plagiarism
    is_plagiarism=false
    if [ "$(check_plagiarism "$student_id")" == "true" ]; then
        is_plagiarism=true
        remarks="plagiarism detected"
        marks1=$((0- (total_marks * plagiarism_penalty / 100)))
    fi

    # Move the evaluated submission to the checked directory
    mv "$working_directory/$student_id" checked/
    if [ "$is_plagiarism" == "true" ]; then
    echo "$student_id,$marks,$((total_marks - marks)),$marks1,$remarks" >> marks.csv
    else
    echo "$student_id,$marks,$((total_marks - marks)),$marks,$remarks" >> marks.csv
    fi

}

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 -i input_file.txt"
    exit 1
fi

input_file=""
while getopts "i:" opt; do
    case $opt in
        i) input_file="$OPTARG" ;;
        *) echo "Invalid option: -$OPTARG" >&2; exit 1 ;;
    esac
done

# Parse the input file
parse_input_file "$input_file"

echo "$student_id_range"

# Create the directories if they don't exist
rm -rf issues checked
mkdir -p issues checked
rm -rf issues/* checked/*
mkdir -p "$working_directory"
resources_directory="resources"
mkdir -p "$resources_directory"
mv "$expected_output_file" "$resources_directory"
mv "$plagiarism_file" "$resources_directory"
expected_output_file="$resources_directory/expected_output.txt"
plagiarism_file="$resources_directory/plagiarism.txt"

# Create the marks file with headers
echo "id,marks,marks_deducted,total_marks,remarks" > marks.csv

# Process each submission
for file in "$working_directory"/*; do
    if [[ "$file" == "$expected_output_file" || "$file" == "$plagiarism_file" ]]; then
        continue
    fi
    student_id=$(basename "$file" | cut -d. -f1)

    range=$(is_student_id_in_range "$student_id" | tr -d '[:space:]')

    
    # Check if the file is an archive with valid student ID format
    file_extension="${file##*.}"
    echo "$file_extension"
    if [ -d "$file" ]; then
        echo "issue case #1: submission is a folder"
    fi
        
   if [ "$use_archive" == "true" ] && [[ ! "$(validate_student_id "$student_id")" == "true" || ( ! " ${allowed_archives[@]} " =~ " ${file_extension} " && ! -d "$file" ) ]]; then

        echo "$student_id,0,0,0,issue case #4: invalid archive file name or extension" >> marks.csv
        mv "$file" issues/
        continue
    fi


    if [ "$use_archive" == "false" ] && [[ ! "$(validate_student_id "$student_id")" == "true" && ! " ${allowed_archives[@]} " =~ " ${file_extension} " && ! -d "$file" ]] && [ ! -d "$file" ]; then
        echo "$student_id,0,0,0,issue case #4: invalid archive file name or extension" >> marks.csv
        mv "$file" issues/
        continue
    fi

    # Check if the student ID within the range
    if [[ "$range" != "true" ]]; then
        echo "$student_id,0,0,0,issue case #5: student ID out of range or invalid format" >> marks.csv
        mv "$file" issues/
        continue
    fi

    process_submission "$student_id" "$file"
done

# Handle missing submissions for student IDs in the range
min_student_id=$(echo "$student_id_range" | cut -d' ' -f1)
max_student_id=$(echo "$student_id_range" | cut -d' ' -f2)

for ((id=min_student_id; id<=max_student_id; id++)); do
    id=$(printf "%07d" "$id")
   if [ ! -d "issues/$id" ] && [ ! -d "checked/$id" ]; then
        echo "$id,0,0,0,missing submission" >> marks.csv
    fi
done
