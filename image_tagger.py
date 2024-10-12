import os
import glob
from flask import Flask, render_template_string, request, redirect, url_for

app = Flask(__name__)

# Configure the directory containing images and tag files
IMAGE_DIR = 'images'
ALLOWED_EXTENSIONS = {'jpg', 'jpeg', 'png', 'gif'}
images = []  # List of image filenames
current_index = 0  # Index of the current image

def load_images():
    global images
    images = sorted([os.path.basename(f) for f in glob.glob(os.path.join(IMAGE_DIR, '*')) if f.split('.')[-1].lower() in ALLOWED_EXTENSIONS])

def get_tags(image_name):
    base_name, _ = os.path.splitext(image_name)
    pattern = os.path.join(IMAGE_DIR, f"{base_name}-*.txt")
    tag_files = glob.glob(pattern)
    tags = {}
    all_tags = set()

    # Load tags from labeled files
    for tag_file in tag_files:
        label = os.path.splitext(os.path.basename(tag_file))[0].replace(f"{base_name}-", "")
        with open(tag_file, 'r') as f:
            tag_list = [tag.strip() for tag in f.read().split(',')]
            tags[label] = tag_list
            all_tags.update(tag_list)

    # Load tags from the unlabeled file, if it exists
    unlabeled_file = os.path.join(IMAGE_DIR, f"{base_name}.txt")
    unlabeled_tags = []
    if os.path.exists(unlabeled_file):
        with open(unlabeled_file, 'r') as f:
            unlabeled_tags = [tag.strip() for tag in f.read().split(',')]
        tags['Current'] = unlabeled_tags  # Show on top and select by default
        all_tags.update(unlabeled_tags)

    # Create a category with all unique tags
    tags['Unique Tags'] = sorted(all_tags)

    return tags, unlabeled_tags

@app.route('/', methods=['GET', 'POST'])
def index():
    global current_index
    if not images:
        load_images()
    if request.method == 'POST':
        action = request.form.get('action')
        # Save selected tags
        selected_tags = request.form.get('selected_tags', '')
        manual_tags = request.form.get('manual_tags', '')
        combined_tags = set(selected_tags.split(', ')) if selected_tags else set()
        combined_tags.update(manual_tags.split('\n'))
        combined_tags = [tag.strip() for tag in combined_tags if tag.strip()]
        base_name, _ = os.path.splitext(images[current_index])
        unlabeled_file = os.path.join(IMAGE_DIR, f"{base_name}.txt")
        with open(unlabeled_file, 'w') as f:
            f.write(', '.join(combined_tags))
        # Navigate to next or previous image
        if action == 'next':
            current_index = (current_index + 1) % len(images)
        elif action == 'prev':
            current_index = (current_index - 1) % len(images)
        return redirect(url_for('index'))

    image_name = images[current_index]
    tags, selected_tags = get_tags(image_name)
    return render_template_string(TEMPLATE, image_name=image_name, tags=tags, selected_tags=selected_tags)

TEMPLATE = '''
<!DOCTYPE html>
<html>
<head>
    <title>Image Tagger</title>
    <style>
        body { display: flex; font-family: Arial, sans-serif; margin: 0; padding: 0; }
        .image { width: 50%; display: flex; align-items: center; justify-content: center; }
        .image img { max-width: 100%; max-height: 100vh; }
        .tags { width: 50%; padding: 20px; box-sizing: border-box; overflow-y: auto; }
        .tag { display: inline-block; padding: 5px 10px; border: 1px solid #ccc; border-radius: 15px; margin: 5px; cursor: pointer; }
        .selected { background-color: #007bff; color: #fff; }
        .category { margin-bottom: 20px; }
        .category-title { font-weight: bold; margin-bottom: 10px; border-bottom: 1px solid #ccc; padding-bottom: 5px; }
        #tag-preview, #manual-tags { width: 100%; box-sizing: border-box; margin-bottom: 20px; }
        #tag-preview { height: 50px; resize: none; }
        #manual-tags { height: 100px; }
        .navigation { margin-top: 20px; }
        .navigation button { padding: 10px 20px; margin-right: 10px; }
    </style>
</head>
<body>
    <div class="image">
        <img src="{{ url_for('static', filename='{{ image_name }}') }}" alt="Image">
    </div>
    <div class="tags">
        <div class="category">
            <div class="category-title">Tag Preview</div>
            <textarea id="tag-preview" readonly>{{ ', '.join(selected_tags) }}</textarea>
        </div>
        <div class="category">
            <div class="category-title">Manual Tags</div>
            <textarea id="manual-tags" placeholder="Enter new tags, one per line"></textarea>
        </div>
        {% for label, tag_list in tags.items() %}
        <div class="category">
            <div class="category-title">{{ label }}</div>
            {% for tag in tag_list %}
            <div class="tag" data-tag="{{ tag }}">{{ tag }}</div>
            {% endfor %}
        </div>
        {% endfor %}
        <div class="navigation">
            <form method="post" id="tag-form">
                <input type="hidden" name="selected_tags" id="selected-tags">
                <input type="hidden" name="manual_tags" id="manual-tags-hidden">
                <button type="button" onclick="navigate('prev')">Previous</button>
                <button type="button" onclick="navigate('next')">Next</button>
            </form>
        </div>
    </div>
    <script>
        const tags = document.querySelectorAll('.tag');
        let selectedTags = new Set({{ selected_tags | tojson | safe }});
        const manualTagsTextarea = document.getElementById('manual-tags');

        function updateTagPreview() {
            const allTags = Array.from(selectedTags);
            const manualTags = manualTagsTextarea.value.split('\\n').map(tag => tag.trim()).filter(tag => tag);
            const combinedTags = allTags.concat(manualTags.filter(tag => !allTags.includes(tag)));
            document.getElementById('tag-preview').value = combinedTags.join(', ');
        }

        function toggleTag(tagElement) {
            const tagText = tagElement.dataset.tag;
            if (tagElement.classList.toggle('selected')) {
                selectedTags.add(tagText);
            } else {
                selectedTags.delete(tagText);
            }
            // Toggle all identical tags
            document.querySelectorAll(`.tag[data-tag='${tagText}']`).forEach(el => {
                el.classList.toggle('selected', tagElement.classList.contains('selected'));
            });
            updateTagPreview();
        }

        tags.forEach(tag => {
            if (selectedTags.has(tag.dataset.tag)) {
                tag.classList.add('selected');
            }
            tag.addEventListener('click', () => toggleTag(tag));
        });

        manualTagsTextarea.addEventListener('input', updateTagPreview);

        function navigate(action) {
            document.getElementById('selected-tags').value = Array.from(selectedTags).join(', ');
            document.getElementById('manual-tags-hidden').value = manualTagsTextarea.value;
            const form = document.getElementById('tag-form');
            const actionInput = document.createElement('input');
            actionInput.type = 'hidden';
            actionInput.name = 'action';
            actionInput.value = action;
            form.appendChild(actionInput);
            form.submit();
        }
    </script>
</body>
</html>
'''

if __name__ == '__main__':
    # Ensure the image directory exists
    if not os.path.isdir(IMAGE_DIR):
        print(f"Image directory '{IMAGE_DIR}' does not exist.")
        exit(1)
    load_images()
    if not images:
        print(f"No images found in '{IMAGE_DIR}' directory.")
        exit(1)
    # Configure static folder to serve images
    app = Flask(__name__, static_folder=IMAGE_DIR)
    app.run(debug=True)
